using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace RpcClient
{
	enum RpcCode
	{
		Ok,
		BadCall,        // - Incorrect args or client was not set up correctly
		BadRemoteCall,  // - Received bad data from peer
		BadConnection,  // - Data failed to send or receive
		InternalError,
	};

	class RemoteClient
	{
		public volatile TcpClient m_tcp;
		private volatile List<byte[]> m_calls;
		private RemoteClass m_class;
		private int m_hedrsize;

		public RemoteClient(TcpClient Client, RemoteClass Klass)
		{
			m_tcp = Client;
			m_calls = new List<byte[]>();
			m_class = Klass;
			m_hedrsize = NetStruct.FmtLen("li", (Int64)0, (int)0);
		}

		// - Returns true if 'Count' bytes were read
		private bool ReadBytes(byte[] Buffer, int Off, int Count)
		{
			NetworkStream stream = m_tcp.GetStream();
			int read = 0, pos = Off;
			do
			{
				read = stream.Read(Buffer, pos, Count - (pos - Off));
				pos += read;
			} while (read > 0 && pos < Off + Count);

			return pos == Off + Count;
		}

		/// <summary>
		/// - Blocking call to receive and run the next command
		/// </summary>
		/// <returns>
		/// - Return == 0 on success, < 0 if stream data is invalid, or > 0 if the client made a bad call
		/// </returns>
		public RpcCode Recv()
		{
			try
			{
				UInt64 hash;
				int argslen;
				List<object> list;
				NetworkStream stream = m_tcp.GetStream();
				byte[] buf = new byte[m_hedrsize];

				if (!ReadBytes(buf, 0, buf.Length))
				{
					Console.WriteLine("RemoteClient.Recv() couldn't receive arg header");
					return RpcCode.BadConnection;
				}

				if ((list = NetStruct.UnpackFmt(buf, 0, "li")) == null)
					return RpcCode.BadRemoteCall;

				Int64 i64 = (Int64)list[0];
				hash = unchecked((UInt64)i64);
				argslen = (int)list[1];
				if (argslen < 0)
					return RpcCode.BadRemoteCall;

				RemoteMethod meth = m_class.FindHash(hash);
				if (meth == null)
				{
					Console.WriteLine($"RemoteClient.Recv() failed to find function hash 0x{hash.ToString("X")}!");
					return RpcCode.BadRemoteCall;
				}

				buf = new byte[argslen];
				if (argslen > 0 && !ReadBytes(buf, 0, buf.Length))
				{
					Console.WriteLine("RemoteClient.Recv() failed to receive call args");
					return RpcCode.BadConnection;
				}

				return meth.Call(buf) == RpcCode.Ok ? RpcCode.Ok : RpcCode.BadRemoteCall; // RemoteMethod handles errors from here*/
			}
			catch (Exception e) { Console.WriteLine(e); }
			return RpcCode.InternalError;
		}

		// - Blocking call to flush all pending commands
		public void Send()
		{
			try
			{
				NetworkStream stream = m_tcp.GetStream();
				foreach (var call in m_calls)
					stream.Write(call, 0, call.Length);
			}
			catch (Exception e)
			{
				if (e is SocketException || e is System.IO.IOException ||
					e is InvalidOperationException)
					Console.WriteLine(e);
				else
					throw;
			}

			m_calls.Clear();
		}

		public bool RemoteCall(string Method, string FmtArgs, params object[] Args)
		{
			UInt64 hash = FNV1a.Hash(Method);
			int argslen = NetStruct.FmtLen(FmtArgs, Args);
			byte[] buf;
			int len;

			if (argslen < 0)
			{
				throw new ArgumentException($"Failed to format '{FmtArgs}' in call to '{Method}'");
				return false;
			}

			buf = new byte[m_hedrsize + argslen];
			len = NetStruct.PackFmtBuffer(buf, 0, "li", unchecked((Int64)hash), argslen);

			if (len != m_hedrsize)
			{
				Console.WriteLine($"RemoteClient.RemoteCall() internal error");
				return false;
			}

			if (argslen > 0)
			{
				if (NetStruct.PackFmtBuffer(buf, len, FmtArgs, Args) < 0)
				{
					Console.WriteLine($"RemoteClient.RemoteCall() failed to pack args '{FmtArgs}'");
					return false;
				}
			}

			m_calls.Add(buf);
			return true;
		}
	}

	class RemoteMethod
	{
		public string m_name { get; }
		public UInt64 m_hash { get; }
		Func<byte[], bool> m_meth { get; }

		public RemoteMethod(string Name, Func<byte[], bool> Method)
		{
			m_name = Name;
			m_hash = FNV1a.Hash(Name);
			m_meth = Method;
		}

		public RpcCode Call(byte[] Args) {
			return m_meth(Args) ? RpcCode.Ok : RpcCode.BadRemoteCall;
		}
	}

	class RemoteClass
	{
		List<RemoteMethod> m_meths;

		public RemoteClass() {
			m_meths = new List<RemoteMethod>();
		}

		public void AddMethod(string Name, Func<byte[], bool> Method) {
			m_meths.Add(new RemoteMethod(Name, Method));
		}
		public RemoteMethod FindHash(UInt64 Hash) {
			return m_meths.Find(m => m.m_hash == Hash);
		}
		public RemoteMethod Find(string Str) {
			return FindHash(FNV1a.Hash(Str));
		}
	}

	static class FNV1a
	{
		// 64-bit FNV1-a: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param

		const UInt64 default_offset_basis = 0xCBF29CE484222325;
		const UInt64 prime = 0x100000001B3;

		public static UInt64 Hash(string Str)
		{

			UInt64 hash = default_offset_basis;
			foreach (char c in Str)
			{
				hash ^= (UInt64)c;
				hash *= prime;
			}
			return hash;
		}
	}
}
