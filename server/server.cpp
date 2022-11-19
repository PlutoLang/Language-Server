#include <fstream>
#include <iostream>

#include <Exception.hpp>
#include <json.hpp>
#include <JsonArray.hpp>
#include <JsonInt.hpp>
#include <JsonObject.hpp>
#include <JsonString.hpp>
#include <os.hpp>
#include <Server.hpp>
#include <ServerService.hpp>
#include <Socket.hpp>
#include <Tempfile.hpp>

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
		string::replace_all(contents, "\r\n", "\n");

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

static void sendResult(Socket& s, int64_t reqid, JsonObject&& result)
{
	JsonObject msg;
	msg.add("jsonrpc", "2.0");
	msg.add("id", reqid);
	msg.add(soup::make_unique<JsonString>("result"), soup::make_unique<JsonObject>(std::move(result)));
	sendWithLen(s, msg.encode());
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

[[nodiscard]] static soup::UniquePtr<JsonNode> encodeLineDiagnostic(const std::string& contents, int64_t line, const std::string& message, int severity = 1)
{
	auto obj = soup::make_unique<JsonObject>();
	obj->add(soup::make_unique<JsonString>("range"), encodeLineRange(contents, line));
	obj->add("message", message);
	obj->add("severity", severity);
	return obj;
}

struct PlutoDiagnostic
{
	unsigned long long line;
	std::string msg;

	operator bool() const noexcept
	{
		return !msg.empty();
	}

	void discharge(const std::string& contents, UniquePtr<JsonArray>& items)
	{
		if (!msg.empty())
		{
			items->children.emplace_back(encodeLineDiagnostic(contents, line, msg, ((msg.substr(0, 9) == "warning: ") ? 2 : 1)));
			msg.clear();
		}
	}
};

[[nodiscard]] static soup::UniquePtr<JsonArray> lint(const std::string& contents)
{
	auto items = soup::make_unique<JsonArray>();

	// Create temp file
	Tempfile tf("lua");

	// Write contents
	std::ofstream of(tf.path);
	of << contents;
	of.close();

	// Lint
	PlutoDiagnostic diag;
	auto res = os::execute("plutoc", { "-p", tf.path.string() });
	for (auto str : string::explode<std::string>(res, "\n"))
	{
		if (str.empty())
		{
			continue;
		}

		if (str.at(0) == ' ') // Diagnostic continues?
		{
			if (diag)
			{
				if (auto sep = str.find("^ here: "); sep != std::string::npos)
				{
					auto here = str.substr(sep + 2);
					if (here.length() > diag.msg.length()) // Heuristically exclude generic here messages
					{
						diag.msg.push_back('\n');
						diag.msg.append(str.substr(sep + 2));
					}
				}
				else if (auto sep = str.find("+ note: "); sep != std::string::npos)
				{
					diag.msg.push_back('\n');
					diag.msg.append(str.substr(sep + 2));
				}
			}
			continue;
		}

		// New diagnostic

		diag.discharge(contents, items);

		str = str.substr(str.find(".lua:") + 5); // erase file name

		auto sep = str.find(": ");
		diag.line = std::stoull(str.substr(0, sep)) - 1;
		diag.msg = str.substr(sep + 2);
	}
	diag.discharge(contents, items);

	return items;
}

static void lintAndPublish(Socket& s, const std::string& uri, const std::string& contents)
{
	JsonObject msg;
	msg.add("uri", uri);
	msg.add(soup::make_unique<JsonString>("diagnostics"), lint(contents));
	sendRequest(s, "textDocument/publishDiagnostics", std::move(msg));
}

static void lintAndSendResult(Socket& s, int64_t reqid, const std::string& contents)
{
	JsonObject msg;
	msg.add("kind", "full");
	msg.add(soup::make_unique<JsonString>("items"), lint(contents));
	sendResult(s, reqid, std::move(msg));
}

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
				cd.len = std::stoull(cd.str.substr(16, sep - 16));
				cd.str.erase(0, sep + 4);
			}

			if (cd.str.length() >= cd.len)
			{
				std::cout << s.peer.toString() << " - " << cd.str << std::endl;

				// Decode
				auto root = json::decodeForDedicatedVariable(cd.str);
				if (!root)
				{
					throw Exception("Received invalid JSON data");
				}

				// Remove region from buffer
				cd.str.erase(0, cd.len);
				cd.len = -1;

				// Process message
				int64_t reqid = -1;
				if (root->asObj().contains("id"))
				{
					reqid = root->asObj().at("id").asInt().value;
				}
				const std::string& method = root->asObj().at("method").asStr().value;

				std::cout << "ID " << reqid << ", method " << method << "\n";

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

					JsonObject msg;
					msg.add(soup::make_unique<JsonString>("capabilities"), std::move(caps));

					sendResult(s, reqid, std::move(msg));

					sendWithLen(s, R"({"jsonrpc":"2.0","method":"window/showMessage","params":{"type":0,"message":"[Pluto Language Server] Socket established."}})");
				}
				else if (method == "textDocument/didOpen")
				{
					const std::string& uri = root->asObj().at("params").asObj().at("textDocument").asObj().at("uri").asStr().value;
					const std::string& text = root->asObj().at("params").asObj().at("textDocument").asObj().at("text").asStr().value;
					//std::cout << uri << " opened: " << text << "\n";
					if (cd.supports_pull_diagnostics)
					{
						cd.updateFileContents(uri, text);
					}
					else
					{
						lintAndPublish(s, uri, text);
					}
				}
				else if (method == "textDocument/didChange")
				{
					const std::string& uri = root->asObj().at("params").asObj().at("textDocument").asObj().at("uri").asStr().value;
					const std::string& text = root->asObj().at("params").asObj().at("contentChanges").asArr().at(0).asObj().at("text").asStr().value;
					//std::cout << uri << " changed: " << text << "\n";
					if (cd.supports_pull_diagnostics)
					{
						cd.updateFileContents(uri, text);
					}
					else
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
					lintAndSendResult(s, reqid, contents);
				}
			}
		}

		recvLoop(s);
	});
}

int main()
{
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
	if (!serv.bind(5007, &srv))
	{
		std::cerr << "Failed to bind to port 5007" << std::endl;
		return 1;
	}
	std::cout << "Pluto Language Server is listening on port 5007." << std::endl;
	serv.run();
	return 0;
}
