using System;
using System.Collections.Generic;
using System.Net;
using System.Text;

namespace RpcClient
{
	enum NetStructFmt
	{
		Bytes = 'B',   // - Pointer to NetStruct_Bytes struct
		String = 's',  // - UTF-8
		Int = 'i',
		Long = 'l',
		Float = 'f',
		Double = 'd',
	};

	enum NetStructCode
	{
		Invalid = -1,
		Bytes = 0,
		String,
		Int,
		Long,
		Float,
		Double,
	};

	static class NetStruct
	{
		public static byte[] PackFmt(string Fmt, params object[] Items)
		{
			int off = 0;
			int fmtlen = FmtLen(Fmt, Items);

			if (fmtlen <= 0)
				return null;

			byte[] bytes = new byte[fmtlen];

			for (int i = 0; i < Fmt.Length; i++)
			{
				int len = PackItem(bytes, off, FmtToCode(Fmt[i]), Items[i]);
				if (len <= 0)
					return null;

				off += len;
			}

			return bytes;
		}

		public static int PackFmtBuffer(byte[] Buffer, int Off, string Fmt, params object[] Items)
		{
			int off = Off;
			int fmtlen = FmtLen(Fmt, Items);

			if (fmtlen <= 0 || fmtlen > Buffer.Length - off)
				return -1;

			for (int i = 0; i < Fmt.Length; i++)
			{
				int len = PackItem(Buffer, off, FmtToCode(Fmt[i]), Items[i]);
				if (len <= 0)
					return -1;

				off += len;
			}

			return off - Off;
		}

		public static List<object> UnpackFmt(byte[] Buffer, int Off, string Fmt)
		{
			List<object> list = new List<object>();

			for (int i = 0; i < Fmt.Length; i++)
			{
				object obj;
				int len = UnpackItem(Buffer, Off, FmtToCode(Fmt[i]), out obj);
				if (len <= 0)
					return null;

				Off += len;
				list.Add(obj);
			}

			return list;
		}

		public static int FmtLen(string Fmt, params object[] Items)
		{
			int len = 0;

			if (Fmt.Length != Items.Length)
				return -1;

			for (int i = 0; i < Fmt.Length; i++)
			{
				NetStructCode code = FmtToCode(Fmt[i]);
				switch (code)
				{
					case NetStructCode.Bytes: len += ItemLen((byte[])Items[i]); break;
					case NetStructCode.String: len += ItemLen((string)Items[i]); break;
					case NetStructCode.Int: len += ItemLen((int)Items[i]); break;
					case NetStructCode.Long: len += ItemLen((Int64)Items[i]); break;
					case NetStructCode.Float: len += ItemLen(Convert.ToSingle(Items[i])); break;
					case NetStructCode.Double: len += ItemLen((double)Items[i]); break;
					default:
						throw new ArgumentException($"Bad item code {code}");
						return -1;
				}
			}
			return len;
		}

		/// <summary>
		/// Packs the item in to 'Buffer' at 'Off'
		/// </summary>
		/// <returns>Length written on success, or value <= 0 on failure</returns>
		public static int PackItem(byte[] Buffer, int Off, NetStructCode Code, object Obj)
		{
			int len = 0;

			switch (Code)
			{
				case NetStructCode.Bytes:
					{
						byte[] bytes = (byte[])Obj;
						if (ItemLen(bytes) > Buffer.Length - Off)
							return -1;
						len = PackItem(Buffer, Off, NetStructCode.Int, bytes.Length);
						if (len <= 0)
							return -1;
						bytes.CopyTo(Buffer, Off + len);
						len += bytes.Length;
						break;
					}
				case NetStructCode.String:
					{
						string str = (string)Obj;
						if (ItemLen(str) > Buffer.Length - Off)
							return -1;

						if (str != null)
						{
							for (; len < str.Length; len++)
								Buffer[Off + len] = unchecked((byte)str[len]);
						}
						Buffer[Off + len] = 0;
						len++;
						break;
					}
				case NetStructCode.Int:
					{
						int i = (int)Obj;
						if (ItemLen(i) > Buffer.Length - Off)
							return -1;

						BitConverter.GetBytes(IPAddress.HostToNetworkOrder(i)).CopyTo(Buffer, Off);
						len += sizeof(int);
						break;
					}
				case NetStructCode.Long:
					{
						Int64 i = (Int64)Obj;
						if (ItemLen(i) > Buffer.Length - Off)
							return -1;

						BitConverter.GetBytes(IPAddress.HostToNetworkOrder(i)).CopyTo(Buffer, Off);
						len += sizeof(Int64);
						break;
					}
				case NetStructCode.Float:
					{
						float f = (float)Convert.ToSingle(Obj);
						if (ItemLen(f) > Buffer.Length - Off)
							return -1;

						byte[] bytes = BitConverter.GetBytes(f);
						if (BitConverter.IsLittleEndian)
							Array.Reverse(bytes);
						bytes.CopyTo(Buffer, Off);
						len += sizeof(Int64);
						break;
					}
				case NetStructCode.Double:
					{
						double d = (double)Obj;
						if (ItemLen(d) > Buffer.Length - Off)
							return -1;

						byte[] bytes = BitConverter.GetBytes(d);
						if (BitConverter.IsLittleEndian)
							Array.Reverse(bytes);
						bytes.CopyTo(Buffer, Off);
						len += sizeof(double);
						break;
					}
				default:
					len = -1;
					throw new ArgumentException($"Bad item code {Code}");
			}

			return len;
		}

		public static int UnpackItem(byte[] Buffer, int Off, NetStructCode Code, out object Obj)
		{
			int len = 0;
			Obj = null;

			switch (Code)
			{
			case NetStructCode.Bytes:
			{
				int count;
				object obj;
				if ((len = UnpackItem(Buffer, Off, Code, out obj)) <= 0)
					return -1;

				count = (int)obj;
				Off += len;
				if (count < 0 || count > Buffer.Length - Off)
					return -1;

				if (count > 0)
					Obj = new byte[0];
				else
				{
					byte[] bytes = new byte[count];
					Array.Copy(Buffer, Off, bytes, 0, count);
					Obj = bytes;
				}
				break;
			}
			case NetStructCode.String:
			{
				for (; len < Buffer.Length - Off && Buffer[len + Off] != 0; len++) ;
				if (len > Buffer.Length - Off || Buffer[len + Off] != 0) // No null-terminator
					return -1;

				if (len == 0)
					Obj = "";
				else
					Obj = Encoding.UTF8.GetString(Buffer, Off, len);
				len++; // Include null-terminator
				break;
			}
			case NetStructCode.Int:
			{
				if (sizeof(int) > Buffer.Length - Off)
					return -1;

				Obj = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(Buffer, Off));
				len += sizeof(int);
				break;
			}
			case NetStructCode.Long:
			{
				if (sizeof(Int64) > Buffer.Length - Off)
					return -1;

				Obj = IPAddress.NetworkToHostOrder(BitConverter.ToInt64(Buffer, Off));
				len += sizeof(Int64);
				break;
			}
			case NetStructCode.Float:
			{
				if (sizeof(float) > Buffer.Length - Off)
					return -1;

				byte[] bytes = new byte[sizeof(float)];
				Array.Copy(Buffer, Off, bytes, 0, bytes.Length);
				if (BitConverter.IsLittleEndian)
					Array.Reverse(bytes);
				
				Obj = BitConverter.ToSingle(bytes);
				len += bytes.Length;
				break;
			}
			case NetStructCode.Double:
			{
				if (sizeof(float) > Buffer.Length - Off)
					return -1;

				byte[] bytes = new byte[sizeof(double)];
				Array.Copy(Buffer, Off, bytes, 0, bytes.Length);
				if (BitConverter.IsLittleEndian)
					Array.Reverse(bytes);
				
				Obj = BitConverter.ToDouble(bytes, Off);
				len += bytes.Length;
				break;
			}
			default:
				len = -1;
				throw new ArgumentException($"Bad item code {Code}");
			}
			return len;
		}

		public static int ItemLen(byte[] Bytes) { return (Bytes != null ? Bytes.Length : 0) + sizeof(int); }
		public static int ItemLen(string Str) { return Str != null ? Str.Length + 1 : 1; }
		public static int ItemLen(int Int) { return sizeof(int); }
		public static int ItemLen(Int64 Long) { return sizeof(Int64); }
		public static int ItemLen(float Fl) { return sizeof(float); }
		public static int ItemLen(double Db) { return sizeof(double); }

		public static NetStructCode FmtToCode(char Fmt)
		{
			switch ((int)Fmt)
			{
				case (int)NetStructFmt.Bytes: return NetStructCode.Bytes;
				case (int)NetStructFmt.String: return NetStructCode.String;
				case (int)NetStructFmt.Int: return NetStructCode.Int;
				case (int)NetStructFmt.Long: return NetStructCode.Long;
				case (int)NetStructFmt.Float: return NetStructCode.Float;
				case (int)NetStructFmt.Double: return NetStructCode.Double;
				default:
					throw new ArgumentException($"Bad format char {Fmt}");
			}
			return NetStructCode.Invalid;
		}
	}
}
