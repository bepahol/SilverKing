package com.ms.silverking.net;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.ms.silverking.io.util.BufferUtil;
import com.ms.silverking.numeric.NumConversion;

/**
 * Bundles an IP address and a port.
 */
public final class IPAndPort implements AddrAndPort, Comparable<IPAndPort> {
    private final long  ipAndPort;
    private InetSocketAddress inetSocketAddress;
    
    public static final int	SIZE_BYTES = IPAddrUtil.IPV4_BYTES + NumConversion.BYTES_PER_SHORT;
    public static final IPAndPort	emptyIPAndPort = new IPAndPort(0, 0); 
    
    public static final List<IPAndPort> emptyList = ImmutableList.of();
    
    private IPAndPort(long ipAndPort) {
        this.ipAndPort = ipAndPort;
    }
    
    public IPAndPort(int ip, int port) {
        this.ipAndPort = (ip << 32) | port;
    }
    
    public IPAndPort(byte[] ip, int offset, int port) {
        this.ipAndPort = ((long)IPAddrUtil.addrToInt(ip, offset) << 32) | (long)port;
    }
        
    public IPAndPort(byte[] ip, int port) {
        this(ip, 0, port);
    }
    
    public IPAndPort(String s) {
        String[]    tokens;
        
        tokens = s.split(":");
        if (tokens.length != 2) {
            throw new RuntimeException("Bad IPAndPort definition: "+ s);
        }
        this.ipAndPort = ((long)IPAddrUtil.addrToInt(IPAddrUtil.stringToAddr(tokens[0])) << 32) | (long)Integer.parseInt(tokens[1]);
    }

    public IPAndPort(String s, int port) {
        this.ipAndPort = ((long)IPAddrUtil.addrToInt(IPAddrUtil.stringToAddr(s)) << 32) | (long)port;
    }
    
    public IPAndPort(String s, String port) {
        this(s, Integer.parseInt(port));
    }
    
    public IPAndPort(byte[] ip) {
        this(ip, 0, NumConversion.bytesToUnsignedShort(ip, 4));
    }
    
    public IPAndPort(InetSocketAddress inetSocketAddress) {
        this(inetSocketAddress.getAddress().getAddress(), inetSocketAddress.getPort());
    }
    
    public static IPAndPort fromByteArray(byte[] ipAndPort, int offset) {
        return new IPAndPort(ipAndPort, offset, NumConversion.bytesToUnsignedShort(ipAndPort, offset + 4));
    }
    
    public static IPAndPort fromByteArray(byte[] ipAndPort) {
        return fromByteArray(ipAndPort, 0);
    }
    
    public static IPAndPort fromLong(long l) {
    	return new IPAndPort(l);
    }
    
    public long toLong() {
    	return ipAndPort;
    }
    
    public byte[] toByteArray() {
        byte[]  ipAndPort;
        
        ipAndPort = new byte[IPAddrUtil.IPV4_BYTES + NumConversion.BYTES_PER_SHORT];
        IPAddrUtil.intToAddr(getIPAsInt(), ipAndPort, 0);
        NumConversion.unsignedShortToBytes(getPort(), ipAndPort, IPAddrUtil.IPV4_BYTES);
        return ipAndPort;
    }
    
	public static IPAndPort fromByteBuffer(ByteBuffer buf, int offset) {
		byte[]	b;
		
		b = new byte[SIZE_BYTES];
		BufferUtil.get(buf, offset, b);
		return fromByteArray(b);
	}

    public byte[] getIP() {
        return IPAddrUtil.intToAddr(getIPAsInt());
    }
    
    public int getIPAsInt() {
        return (int)(ipAndPort >>> 32);
    }
    
    public String getIPAsString() {
        return IPAddrUtil.addrToString(getIP());
    }

    public int getPort() {
        return (int)ipAndPort;
    }
    
    public IPAndPort port(int port) {
    	return new IPAndPort(getIP(), port);
    }
    
    public void computeInetAddress() throws UnknownHostException {
        inetSocketAddress = new InetSocketAddress(InetAddress.getByAddress(getIP()), getPort());
    }
    
    @Override
    public InetSocketAddress toInetSocketAddress() throws UnknownHostException {
        if (inetSocketAddress == null) {
            computeInetAddress();
        }   
        return inetSocketAddress;
    }
    
    @Override
    public int compareTo(IPAndPort o) {
        if (ipAndPort < o.ipAndPort) {
            return -1;
        } else if (ipAndPort > o.ipAndPort) {
            return 1;
        } else {
            return 0;
        }
    }
    
    @Override
    public int hashCode() {
        return (int)(ipAndPort >> 32) ^ (int)ipAndPort; 
    }
    
    @Override
    public boolean equals(Object other) {
        IPAndPort   o;
        
        o = (IPAndPort)other;
        //System.out.println(o +" == "+ this);
        return o.ipAndPort == this.ipAndPort; 
    }
    
    @Override
    public String toString() {
        return IPAddrUtil.addrToString(getIP()) +":"+ getPort();
    }
    
    public static String arrayToString(IPAndPort[] ips) {
        StringBuilder   sb;
        
        sb = new StringBuilder();
        for (int i = 0; i < ips.length - 1; i++) {
            sb.append(ips[i]);
            sb.append(',');
        }
        if (ips.length > 0) {
            sb.append(ips[ips.length - 1]);
        } else {
            sb.append("<empty>");
        }
        return sb.toString();
    }
    
    public static List<IPAndPort> list(List<String> defs) {
        List<IPAndPort> l;
        
        l = new ArrayList<>(defs.size());
        for (String def : defs) {
            l.add(new IPAndPort(def));
        }
        return l;
    }
    
    public static Set<IPAndPort> set(Set<String> defs) {
        ImmutableSet.Builder<IPAndPort>	sb;
        
        sb = ImmutableSet.builder();
        for (String def : defs) {
            sb.add(new IPAndPort(def));
        }
        return sb.build();
    }
    
    public static Set<IPAndPort> set(Set<String> servers, int port) {
        ImmutableSet.Builder<IPAndPort>	sb;
        
        sb = ImmutableSet.builder();
        for (String server : servers) {
            sb.add(new IPAndPort(server, port));
        }
        return sb.build();
    }
    
    public static Set<String> copyServerIPsAsMutableSet(Set<IPAndPort> servers) {
    	Set<String>	_servers;
    	
    	_servers = new HashSet<>(servers.size());
    	for (IPAndPort server : servers) {
    		_servers.add(server.getIPAsString());
    	}
    	return _servers;
    }
    
    public static Set<IPAndPort> copyIPAndPortsAsMutableSet(Set<String> servers, int port) {
    	Set<IPAndPort>	_servers;
    	
    	_servers = new HashSet<>(servers.size());
    	for (String server : servers) {
    		_servers.add(new IPAndPort(server, port));
    	}
    	return _servers;
    }
    
    public static void main(String[] args) {
        System.out.println(new IPAndPort("1.2.3.4:9999"));
    }
}
