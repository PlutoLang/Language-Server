import * as path from 'path';
import { workspace, ExtensionContext, window } from 'vscode';
import {
	LanguageClient,
	LanguageClientOptions,
	StreamInfo,
} from 'vscode-languageclient/node';
const net = require('node:net');

let probableServerExecutablePath: string;
let client: LanguageClient;
let socket: any;
let showedFailNotify: boolean = false;

function loopEstablishSocket()
{
	socket = net.connect({
		host: "127.0.0.1",
		port: 5007
	});
	socket.on("connect", function()
	{
		let serverOptions = () => {
			let result: StreamInfo = {
				writer: socket,
				reader: socket
			};
			return Promise.resolve(result);
		};

		// Options to control the language client
		let clientOptions: LanguageClientOptions = {
			// Register the server for plain text documents
			documentSelector: [{ scheme: 'file', language: 'lua' }],
			synchronize: {
				fileEvents: workspace.createFileSystemWatcher('**/*.*')
			}
		};

		// Create the language client and start the client.
		client = new LanguageClient(
			'plutoLanguageServer',
			'Pluto Language Server',
			serverOptions,
			clientOptions
		);

		// Start the client.
		client.start();
	});
	socket.on("close", function()
	{
		if (!showedFailNotify)
		{
			window.showInformationMessage("[Pluto Language Server] Failed to establish socket to server. If it's not running, you might find the executable at " + probableServerExecutablePath);
			showedFailNotify = true;
		}
		loopEstablishSocket();
	});
}

export function activate(context: ExtensionContext)
{
	probableServerExecutablePath = context.asAbsolutePath(path.join("server.exe"));

	loopEstablishSocket();
}

export function deactivate()
{
	if (client)
	{
		client.stop();
	}
	socket.close();
}
