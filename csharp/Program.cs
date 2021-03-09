using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using RpcClient;

namespace csharp
{
	class Program
	{
		static void Main(string[] args)
		{
			new Thread(DemoServer.Start).Start();
			Console.WriteLine(
				"Running demo server...\n" +
				"Wait for a client, or press any key to run the C# demo client.");
			Console.ReadKey();
			new Thread(DemoClient.Start).Start();
		}
	}

	static class DemoServer
	{
		public static bool doListen = false;

		public static void Start()
		{
			TcpListener tcp = new TcpListener(IPAddress.Loopback, 11223);
			tcp.Start();
			doListen = true;

			RemoteClass myclass = new RemoteClass();
			myclass.AddMethod("Say_IntString", args =>
			{
				List<object> list = NetStruct.UnpackFmt(args, 0, "isf");
				if (list == null)
					return false;

				int i = (int)list[0];
				string s = (string)list[1];
				float f = (float)list[2];
				Console.WriteLine($"Say_IntString(): {i}, '{s}', {f}");
				return true;
			});
			myclass.AddMethod("CloseServer", args =>
			{
				Console.WriteLine("CloseServer(): Got request to close the server");
				doListen = false;
				return true;
			});

			do
			{
				TcpClient client = tcp.AcceptTcpClient();
				RemoteClient remote = new RemoteClient(client, myclass);
				double flLast = 0.5;
				while (remote.Recv() == RpcCode.Ok)
				{
					remote.RemoteCall("wazzap", "sf", $"String {flLast}", flLast);
					flLast += 0.5;
					remote.Send();
				}
				client.Close();
			} while (doListen);

			Console.WriteLine("Stopping server...");
			tcp.Stop();
			doListen = false;
		}
	}

	static class DemoClient
	{
		public static void Start()
		{
			TcpClient tcp = new TcpClient();
			try
			{
				tcp.Connect(IPAddress.Loopback, 11223);
			}
			catch (Exception e)
			{
				Console.WriteLine(e);
				tcp.Close();
				return;
			}

			RemoteClass myclass = new RemoteClass();
			myclass.AddMethod("wazzap", args =>
			{
				List<object> list = NetStruct.UnpackFmt(args, 0, "sf");
				if (list == null)
					return false;

				Console.WriteLine($"wazzap(): '{(string)list[0]}', '{(float)list[1]}'");
				return true;
			});

			RemoteClient client = new RemoteClient(tcp, myclass);
			client.RemoteCall("Say_IntString", "isf", 010011, "gANGSTA!", (float)4.20002);
			client.Send();
			client.Recv();
			client.RemoteCall("CloseServer", "");
			client.Send();
			client.Recv();
			tcp.Close();
		}
	}
}
