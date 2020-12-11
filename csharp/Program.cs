using System;
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
			myclass.AddMethod("Say_IntString", "isf", args =>
			{
				Int64 i = ((NetInt)args.value[0]).value;
				string s = ((NetString)args.value[1]).value;
				double f = ((NetFloat)args.value[2]).value;

				Console.WriteLine($"Say_IntString(): {i}, '{s}', {f}");
				return true;
			});
			myclass.AddMethod("CloseServer", "", args =>
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
				while (remote.Recv() >= 0)
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
			myclass.AddMethod("wazzap", "sf", args =>
			{
				Console.WriteLine($"wazzap(): '{((NetString)args.value[0]).value}', '{((NetFloat)args.value[1]).value}'");
				return true;
			});

			RemoteClient client = new RemoteClient(tcp, myclass);
			client.RemoteCall("Say_IntString", "isf", 80085, "gANGSTA!", 4.20002);
			client.Send();
			client.Recv();
			client.RemoteCall("CloseServer", "");
			client.Send();
			client.Recv();
			tcp.Close();
		}
	}
}
