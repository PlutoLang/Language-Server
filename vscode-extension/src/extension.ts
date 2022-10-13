import { getVSCodeDownloadUrl } from '@vscode/test-electron/out/util';
import { ChildProcess } from 'child_process';
import { privateEncrypt } from 'crypto';
import * as path from 'path';
import { workspace, ExtensionContext } from 'vscode';
import {
	LanguageClient,
	LanguageClientOptions,
	StreamInfo,
} from 'vscode-languageclient/node';
const net = require('node:net');
// eslint-disable-next-line @typescript-eslint/naming-convention
const child_process = require('node:child_process');

let client: LanguageClient;
let server: ChildProcess;

export function activate(context: ExtensionContext)
{
	server = child_process.exec(context.asAbsolutePath(path.join("../server/server.exe")));

	let serverOptions = () => {
		// Connect to language server via socket
		let socket = net.connect({
			host: "127.0.0.1",
			port: 5007
		});
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
	
	// Start the client. This will also launch the server
	client.start();
}

export function deactivate()
{
	if (client)
	{
		client.stop();
	}
	server.kill();
}
