import { ChildProcess } from 'child_process';
import * as path from 'path';
import { workspace, ExtensionContext } from 'vscode';
import {
	LanguageClient,
	LanguageClientOptions,
	StreamInfo,
} from 'vscode-languageclient/node';
const net = require('node:net');
const child_process = require('node:child_process');

let proc: ChildProcess;
let timeout: any;
let client: LanguageClient;
let socket: any;

export function activate(context: ExtensionContext)
{
	proc = child_process.spawn(context.asAbsolutePath(path.join("pluto-language-server.exe")), [
		"--plutoc", context.asAbsolutePath(path.join("plutoc.exe")),
		"--port", 9171
	]);

	// Delay a bit so this garbage won't attempt to connect to a process that hasn't started yet
	timeout = setTimeout(function()
	{
		timeout = undefined;
		socket = net.connect({
			host: "127.0.0.1",
			port: 9171
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
				documentSelector: [
					{ scheme: 'file', language: 'lua' },
					{ scheme: 'file', language: 'pluto' },
				],
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
	}, 500);
}

export function deactivate()
{
	if (timeout)
	{
		clearTimeout(timeout);
	}
	if (client)
	{
		client.stop();
	}
	if (socket)
	{
		socket.close();
	}
	proc.kill();
}
