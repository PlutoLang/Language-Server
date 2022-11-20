import * as path from 'path';
import { workspace, ExtensionContext, window } from 'vscode';
import {
	LanguageClient,
	LanguageClientOptions,
	StreamInfo,
} from 'vscode-languageclient/node';
const net = require('node:net');

let client: LanguageClient;
let socket: any;
let showedFailNotify: boolean = false;

function loopEstablishSocket()
{
	socket = net.connect({
		host: "127.0.0.1",
		port: 9170
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
			window.showInformationMessage("[Pluto Language Server] Failed to establish socket to server. Make sure it's running!");
			showedFailNotify = true;
		}
		loopEstablishSocket();
	});
}

export function activate(context: ExtensionContext)
{
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
