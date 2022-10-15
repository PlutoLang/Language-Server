import { getVSCodeDownloadUrl } from '@vscode/test-electron/out/util';
import { ChildProcess } from 'child_process';
import { privateEncrypt } from 'crypto';
import * as path from 'path';
import { workspace, ExtensionContext, window } from 'vscode';
import {
	LanguageClient,
	LanguageClientOptions,
	StreamInfo,
} from 'vscode-languageclient/node';
const net = require('node:net');
// eslint-disable-next-line @typescript-eslint/naming-convention
const child_process = require('node:child_process');

let probableServerExecutablePath: string;
let client: LanguageClient;
let socket: any;
let socketState = 0;

function loopEstablishSocket()
{
	socket = net.connect({
		host: "127.0.0.1",
		port: 5007
	});
	socket.on("connect", function()
	{
		socketState = 2;

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
		if (socketState !== 2)
		{
			if (socketState === 0)
			{
				window.showInformationMessage("[Pluto Language Server] Failed to establish socket to server. If it's not running, you might find the executable at " + probableServerExecutablePath);
				socketState = 1;
			}
			loopEstablishSocket();
		}
	});
}

export function activate(context: ExtensionContext)
{
	probableServerExecutablePath = context.asAbsolutePath(path.join("server.exe"));

	socketState = 0;
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
