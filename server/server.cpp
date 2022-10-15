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

struct ClientData
{
	// Message buffering
	std::string str{};
	size_t len = -1;

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

[[nodiscard]] static soup::UniquePtr<JsonNode> encodeLineDiagnostic(const std::string& contents, int64_t line, const std::string& message)
{
	auto obj = soup::make_unique<JsonObject>();
	obj->add(soup::make_unique<JsonString>("range"), encodeLineRange(contents, line));
	obj->add("message", message);
	return obj;
}

static void lintAndSendResult(Socket& s, int64_t reqid, const std::string& contents)
{
	auto items = soup::make_unique<JsonArray>();

	// Create temp file
	Tempfile tf("lua");

	// Write contents
	std::ofstream of(tf.path);
	of << contents;
	of.close();

	// Lint
	auto res = os::execute("plutolint", { tf.path.string() });
	if (!res.empty())
	{
		res = res.substr(0, res.find('\n')); // erase additional lines
		res = res.substr(res.find("]:") + 2); // erase file name

		auto sep = res.find(": ");
		auto line = std::stoull(res.substr(0, sep)) - 1;
		items->children.emplace_back(encodeLineDiagnostic(contents, line, res.substr(sep + 2)));
	}

	JsonObject msg;
	msg.add("kind", "full");
	msg.add(soup::make_unique<JsonString>("items"), std::move(items));
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
					auto caps = soup::make_unique<JsonObject>();
					caps->add("textDocumentSync", 1);
					caps->add("diagnosticProvider", true);

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
					cd.updateFileContents(uri, text);
				}
				else if (method == "textDocument/didChange")
				{
					const std::string& uri = root->asObj().at("params").asObj().at("textDocument").asObj().at("uri").asStr().value;
					const std::string& text = root->asObj().at("params").asObj().at("contentChanges").asArr().at(0).asObj().at("text").asStr().value;
					//std::cout << uri << " changed: " << text << "\n";
					cd.updateFileContents(uri, text);
				}
				else if (method == "textDocument/diagnostic")
				{
					const std::string& uri = root->asObj().at("params").asObj().at("textDocument").asObj().at("uri").asStr().value;
					const std::string& contents = cd.files.at(uri);
					//std::cout << "Diagnostic requested with contents = " << contents << "\n";
					lintAndSendResult(s, reqid, contents);
				}
				else if (method == "shutdown")
				{
					std::cout << "Client requested shutdown, I guess we'll honour this request.\n";
					exit(0);
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
