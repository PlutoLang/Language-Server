#include <fstream>
#include <iostream>

#include <Exception.hpp>
#include <json.hpp>
#include <JsonArray.hpp>
#include <JsonInt.hpp>
#include <JsonObject.hpp>
#include <JsonString.hpp>
#include <main.hpp>
#include <os.hpp>
#include <Server.hpp>
#include <ServerService.hpp>
#include <Socket.hpp>
#include <string.hpp>
#include <Tempfile.hpp>

#include "lsp.hpp"

using namespace soup;

#define FORCE_PUSH_DIAGNOSTICS false

struct ClientData
{
	// Message buffering
	std::string str{};
	size_t len = -1;

	// Capabilities
	bool supports_pull_diagnostics = false;

	// File tracking
	std::unordered_map<std::string, std::string> files{};

	void updateFileContents(const std::string& uri, std::string contents)
	{
		string::replaceAll(contents, "\r\n", "\n");

		if (auto e = files.find(uri); e != files.end())
		{
			e->second = contents;
		}
		else
		{
			files.emplace(uri, contents);
		}
	}
};

static void sendWithLen(Socket& s, const std::string& str)
{
	std::cout << "Sending message: " << str << "\n";
	std::string msg = "Content-Length: ";
	msg.append(std::to_string(str.length()));
	msg.append("\r\n\r\n");
	msg.append(str);
	s.send(msg);
}

static void sendRequest(Socket& s, const std::string& method, JsonObject&& params)
{
	JsonObject msg;
	msg.add("jsonrpc", "2.0");
	msg.add("method", method);
	msg.add(soup::make_unique<JsonString>("params"), soup::make_unique<JsonObject>(std::move(params)));
	sendWithLen(s, msg.encode());
}

static void sendResult(Socket& s, UniquePtr<JsonNode>&& reqid)
{
	JsonObject msg;
	msg.add("jsonrpc", "2.0");
	msg.add("id", std::move(reqid));
	sendWithLen(s, msg.encode());
}

static void sendResult(Socket& s, UniquePtr<JsonNode>&& reqid, UniquePtr<JsonNode>&& result)
{
	JsonObject msg;
	msg.add("jsonrpc", "2.0");
	msg.add("id", std::move(reqid));
	msg.add(soup::make_unique<JsonString>("result"), std::move(result));
	sendWithLen(s, msg.encode());
}

static void sendResult(Socket& s, UniquePtr<JsonNode>&& reqid, JsonObject&& result)
{
	sendResult(s, std::move(reqid), soup::make_unique<JsonObject>(std::move(result)));
}

// line & character start at 0!
[[nodiscard]] static soup::UniquePtr<JsonNode> encodePosition(int64_t line, int64_t character)
{
	auto obj = soup::make_unique<JsonObject>();
	obj->add("line", line);
	obj->add("character", character);
	return obj;
}

[[nodiscard]] static int64_t getLineLength(const std::string& contents, int64_t line)
{
	auto lines = string::explode(contents, "\n");
	SOUP_IF_LIKELY (line < lines.size())
	{
		return lines.at(line).size();
	}
	return 1;
}

[[nodiscard]] static soup::UniquePtr<JsonNode> encodeLineRange(const std::string& contents, int64_t line)
{
	auto obj = soup::make_unique<JsonObject>();
	obj->add(soup::make_unique<JsonString>("start"), encodePosition(line, 0));
	obj->add(soup::make_unique<JsonString>("end"), encodePosition(line, getLineLength(contents, line)));
	return obj;
}

#define PROVIDE_DIAGNOSTIC_SOURCE_AND_CODE false

[[nodiscard]] static soup::UniquePtr<JsonNode> encodeLineDiagnostic(const std::string& contents, int64_t line, const std::string& message, int severity)
{
	auto obj = soup::make_unique<JsonObject>();
	obj->add(soup::make_unique<JsonString>("range"), encodeLineRange(contents, line));
	obj->add("message", message);
	obj->add("severity", severity);
#if PROVIDE_DIAGNOSTIC_SOURCE_AND_CODE
	obj->add("source", "the-source");
	obj->add("code", "the-code");
	if (severity == 1)
	{
		auto codeDescription = soup::make_unique<JsonObject>();
		codeDescription->add("href", "https://plutolang.github.io");
		obj->add(soup::make_unique<JsonString>("codeDescription"), std::move(codeDescription));
	}
#endif
	return obj;
}

struct PlutoHint
{
	enum Type : uint8_t
	{
		DIAGNOSTIC,
		COMPLETIONS,
	};

	Type type;
	soup::UniquePtr<JsonNode> node;
};

struct PlutoDiagnosticBuffer
{
	unsigned long long line;
	std::string msg;

	operator bool() const noexcept
	{
		return !msg.empty();
	}

	void discharge(const std::string& contents, std::vector<PlutoHint>& hints)
	{
		if (!msg.empty())
		{
			int severity = 1;
			if (msg.substr(0, 9) == "warning: ")
			{
				msg.erase(0, 9);
				severity = 2;
			}
			hints.emplace_back(PlutoHint{ PlutoHint::DIAGNOSTIC, encodeLineDiagnostic(contents, line, msg, severity) });
			msg.clear();
		}
	}
};

static std::string plutoc_path = "plutoc";

[[nodiscard]] static std::vector<PlutoHint> getHints(const std::string& contents)
{
	std::vector<PlutoHint> hints;

	// Create temp file
	Tempfile tf("lua");

	// Write contents
	std::ofstream of(tf.path);
	of << contents;
	of.close();

	// Parse
	PlutoDiagnosticBuffer buf;
	auto res = os::execute(plutoc_path, { "-p", tf.path.string() });
	for (auto str : string::explode<std::string>(res, "\n"))
	{
		if (str.empty())
		{
			continue;
		}

		if (str.at(0) == ' ') // Response continues?
		{
			if (buf)
			{
				if (auto sep = str.find("^ here: "); sep != std::string::npos)
				{
					auto here = str.substr(sep + 2);
					if (here.length() > buf.msg.length()) // Heuristically exclude generic here messages
					{
						buf.msg.push_back('\n');
						buf.msg.append(str.substr(sep + 2));
					}
				}
				else if (auto sep = str.find("+ note: "); sep != std::string::npos)
				{
					buf.msg.push_back('\n');
					buf.msg.append(str.substr(sep + 2));
				}
			}
			continue;
		}

		// New response

		buf.discharge(contents, hints);

		if (str.substr(0, 9) == "suggest: ")
		{
			auto completions = soup::make_unique<JsonArray>();
			for (const auto& suggestion : string::explode(str.substr(9), ';'))
			{
				auto arr = string::explode(suggestion, ',');

				auto completion = soup::make_unique<JsonObject>();
				if (arr.at(0) == "local")
				{
					completion->add("kind", CompletionItemKind::Variable);
					completion->add("detail", "local " + arr.at(1));
				}
				else if (arr.at(0) == "stat")
				{
					completion->add("kind", CompletionItemKind::Keyword);
				}
				else if (arr.at(0) == "efunc")
				{
					arr.at(1) += "()";
					completion->add("kind", CompletionItemKind::Function);
				}
				else if (arr.at(0) == "eprop")
				{
					completion->add("kind", CompletionItemKind::EnumMember);
					completion->add("detail", arr.at(1) + " = " + arr.at(2));
				}
				completion->add("label", arr.at(1));
				completions->children.emplace_back(std::move(completion));
			}
			hints.emplace_back(PlutoHint{ PlutoHint::COMPLETIONS, std::move(completions) });
			continue;
		}

		if (auto off = str.find(".lua:"); off != std::string::npos)
		{
			str = str.substr(off + 5);

			auto sep = str.find(": ");
			buf.line = std::stoull(str.substr(0, sep)) - 1;
			buf.msg = str.substr(sep + 2);
		}
		else
		{
			off = str.find(".exe:");
			SOUP_ASSERT(off != std::string::npos, "Failed to parse error");
			str = str.substr(off + 5);

			auto sep = str.find(" on line ");
			buf.line = std::stoull(str.substr(sep + 9)) - 1;
			buf.msg = str.substr(0, sep);
		}
	}
	buf.discharge(contents, hints);

	return hints;
}

[[nodiscard]] static soup::UniquePtr<JsonArray> lint(const std::string& contents)
{
	auto items = soup::make_unique<JsonArray>();
	for (auto& resp : getHints(contents))
	{
		if (resp.type == PlutoHint::DIAGNOSTIC)
		{
			items->children.emplace_back(std::move(resp.node));
		}
	}
	return items;
}

static void lintAndPublish(Socket& s, const std::string& uri, const std::string& contents)
{
	JsonObject msg;
	msg.add("uri", uri);
	msg.add(soup::make_unique<JsonString>("diagnostics"), lint(contents));
	sendRequest(s, "textDocument/publishDiagnostics", std::move(msg));
}

static void lintAndSendResult(Socket& s, UniquePtr<JsonNode>&& reqid, const std::string& contents)
{
	JsonObject msg;
	msg.add("kind", "full");
	msg.add(soup::make_unique<JsonString>("items"), lint(contents));
	sendResult(s, std::move(reqid), std::move(msg));
}

static bool honour_exit = false;

static void recvLoop(Socket& s)
{
	s.recv([](Socket& s, std::string&& data, Capture&&)
	{
		//std::cout << s.peer.toString() << " - " << data << std::endl;

		ClientData& cd = s.custom_data.getStructFromMap(ClientData);
		cd.str.append(data);

		while (cd.str.substr(0, 16) == "Content-Length: " || cd.str.length() >= cd.len)
		{
			if (cd.str.substr(0, 16) == "Content-Length: ")
			{
				auto sep = cd.str.find("\r\n\r\n", 16);
				if (sep == std::string::npos)
				{
					break;
				}
				cd.len = std::stoull(cd.str.substr(16, sep - 16));
				cd.str.erase(0, sep + 4);
			}

			if (cd.str.length() >= cd.len)
			{
				std::cout << s.peer.toString() << " - " << cd.str.substr(0, cd.len) << std::endl;

				// Decode
				auto root = json::decode(cd.str.substr(0, cd.len));
				if (!root)
				{
					throw Exception("Received invalid JSON data");
				}

				// Remove region from buffer
				cd.str.erase(0, cd.len);
				cd.len = -1;

				// Process message
				UniquePtr<JsonNode> reqid;
				if (root->asObj().contains("id"))
				{
					reqid = std::move(*root->asObj().findUp(JsonString("id")));
				}
				else
				{
					reqid = soup::make_unique<JsonInt>(-1);
				}
				const std::string& method = root->asObj().at("method").asStr().value;

				std::cout << "ID " << reqid->encode() << ", method " << method << "\n";

				if (method == "initialize")
				{
#if FORCE_PUSH_DIAGNOSTICS
					cd.supports_pull_diagnostics = false;
#else
					cd.supports_pull_diagnostics = root->asObj().at("params").asObj().at("capabilities").asObj().at("textDocument").asObj().contains("diagnostic");
#endif

					auto caps = soup::make_unique<JsonObject>();
					caps->add("textDocumentSync", 1);
#if !FORCE_PUSH_DIAGNOSTICS
					caps->add("diagnosticProvider", true);
#endif
					{
						auto obj = soup::make_unique<JsonObject>();
						{
							auto triggerCharacters = soup::make_unique<JsonArray>();
							triggerCharacters->children.emplace_back(soup::make_unique<JsonString>("."));
							triggerCharacters->children.emplace_back(soup::make_unique<JsonString>(":"));
							obj->add("triggerCharacters", std::move(triggerCharacters));
						}
						caps->add("completionProvider", std::move(obj));
					}

					JsonObject msg;
					msg.add(soup::make_unique<JsonString>("capabilities"), std::move(caps));

					sendResult(s, std::move(reqid), std::move(msg));

					//sendWithLen(s, R"({"jsonrpc":"2.0","method":"window/showMessage","params":{"type":0,"message":"[Pluto Language Server] Socket established."}})");
				}
				else if (method == "textDocument/didOpen")
				{
					const std::string& uri = root->asObj().at("params").asObj().at("textDocument").asObj().at("uri").asStr().value;
					const std::string& text = root->asObj().at("params").asObj().at("textDocument").asObj().at("text").asStr().value;
					//std::cout << uri << " opened: " << text << "\n";
					cd.updateFileContents(uri, text);
					if (!cd.supports_pull_diagnostics)
					{
						lintAndPublish(s, uri, text);
					}
				}
				else if (method == "textDocument/didChange")
				{
					const std::string& uri = root->asObj().at("params").asObj().at("textDocument").asObj().at("uri").asStr().value;
					const std::string& text = root->asObj().at("params").asObj().at("contentChanges").asArr().at(0).asObj().at("text").asStr().value;
					//std::cout << uri << " changed: " << text << "\n";
					cd.updateFileContents(uri, text);
					if (!cd.supports_pull_diagnostics)
					{
						lintAndPublish(s, uri, text);
					}
				}
				else if (method == "textDocument/diagnostic")
				{
					if (!cd.supports_pull_diagnostics)
					{
						throw Exception("Client did not indicate support for pull diagnostics but attempted to pull diagnostics");
					}
					const std::string& uri = root->asObj().at("params").asObj().at("textDocument").asObj().at("uri").asStr().value;
					const std::string& contents = cd.files.at(uri);
					//std::cout << "Diagnostic requested with contents = " << contents << "\n";
					lintAndSendResult(s, std::move(reqid), contents);
				}
				else if (method == "textDocument/completion")
				{
					// "params":{"textDocument":{"uri":"file:///..."},"position":{"line":8,"character":1},"context":{"triggerKind":1}}

					const std::string& uri = root->asObj().at("params").asObj().at("textDocument").asObj().at("uri").asStr().value;
					const std::string& contents = cd.files.at(uri);
					const int64_t position_line = root->asObj().at("params").asObj().at("position").asObj().at("line").asInt();
					const int64_t position_character = root->asObj().at("params").asObj().at("position").asObj().at("character").asInt();

					std::string modconts{};
					{
						auto lines = string::explode(contents, '\n');
						{
							std::string& line = lines.at(position_line);
							auto cur = position_character;
							if (cur == 0)
							{
								cur = 1;
							}
							bool has_filter = false;
							while (--cur, true)
							{
								if (line.at(cur) == ' '
									|| line.at(cur) == '.'
									|| line.at(cur) == ':'
									)
								{
									++cur;
									break;
								}
								has_filter = true;

								if (cur == 0)
								{
									break;
								}
							}
							if (has_filter)
							{
								line.insert(cur, "pluto_suggest_1 ");
							}
							else
							{
								line.insert(cur, "pluto_suggest_0");
							}
						}
						modconts = string::join(lines, '\n');
					}

					//std::cout << modconts;

					auto completions = soup::make_unique<JsonArray>();
					for (auto& resp : getHints(modconts))
					{
						if (resp.type == PlutoHint::COMPLETIONS)
						{
							completions = std::move(resp.node);
						}
					}
					sendResult(s, std::move(reqid), std::move(completions));
				}
				else if (method == "shutdown")
				{
					// The LSP spec requires that we stop accepting requests from the client at this point,
					// but we only care to go along with a client's shutdown/exit flow, so this is fine.
					sendResult(s, std::move(reqid));
				}
				else if (method == "exit")
				{
					if (honour_exit)
					{
						exit(0);
					}
					// Client assumes that it can restart this server, so we're not actually exitting.
					s.close();
				}
			}
		}

		recvLoop(s);
	});
}

int entry(std::vector<std::string>&& args, bool console)
{
	uint16_t port = 9170;

	for (size_t i = 1; i != args.size(); ++i)
	{
		if (args.at(i) == "--plutoc" && i + 1 != args.size())
		{
			plutoc_path = args.at(++i);
			continue;
		}
		if (args.at(i) == "--port" && i + 1 != args.size())
		{
			port = (uint16_t)std::stoi(args.at(++i));
			continue;
		}
		if (args.at(i) == "--honour-exit" || args.at(i) == "--honor-exit")
		{
			honour_exit = true;
			continue;
		}
		std::cout << "Arguments: --plutoc [path], --port [port], --honour-exit" << std::endl;
		return 2;
	}

	soup::Server serv{};
	serv.on_work_done = [](soup::Worker& w, soup::Scheduler&)
	{
		std::cout << reinterpret_cast<soup::Socket&>(w).peer.toString() << " - work done" << std::endl;
	};
	serv.on_connection_lost = [](soup::Socket& s, soup::Scheduler&)
	{
		std::cout << s.peer.toString() << " - connection lost" << std::endl;
	};
	serv.on_exception = [](soup::Worker& w, const std::exception& e, soup::Scheduler&)
	{
		std::cout << reinterpret_cast<soup::Socket&>(w).peer.toString() << " - exception: " << e.what() << std::endl;
	};

	soup::ServerService srv{
		[](Socket& s, ServerService&, Server&)
		{
			std::cout << s.peer.toString() << " - connection established" << std::endl;

			recvLoop(s);
		}
	};
	if (!serv.bind(port, &srv))
	{
		std::cerr << "Failed to bind to port " << port << std::endl;
		return 1;
	}
	std::cout << "Pluto Language Server is listening on port " << port << std::endl;
	serv.run();
	return 0;
}

SOUP_MAIN_CLI(entry);
