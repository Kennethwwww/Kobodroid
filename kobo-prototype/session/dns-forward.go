// Loopback DNS forwarder for the guest. Without netd, Android 7 bionic resolves locally and,
// with no configured servers, queries 127.0.0.1:53. Queries go to the host's current
// resolv.conf servers, so DHCP changes apply. Exits when the stop file appears.
package main

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"strings"
	"time"
)

func upstreams() []string {
	f, err := os.Open("/etc/resolv.conf")
	if err != nil {
		return nil
	}
	defer f.Close()
	var servers []string
	s := bufio.NewScanner(f)
	for s.Scan() {
		fields := strings.Fields(s.Text())
		if len(fields) >= 2 && fields[0] == "nameserver" && !net.ParseIP(fields[1]).IsLoopback() {
			servers = append(servers, net.JoinHostPort(fields[1], "53"))
		}
	}
	return servers
}

func exchange(network string, query []byte) []byte {
	for _, server := range upstreams() {
		c, err := net.DialTimeout(network, server, 3*time.Second)
		if err != nil {
			continue
		}
		c.SetDeadline(time.Now().Add(5 * time.Second))
		var reply []byte
		if network == "udp" {
			if _, err = c.Write(query); err == nil {
				buf := make([]byte, 4096)
				var n int
				if n, err = c.Read(buf); err == nil {
					reply = buf[:n]
				}
			}
		} else {
			reply, err = tcpExchange(c, query)
		}
		c.Close()
		if err == nil {
			return reply
		}
	}
	return nil
}

func tcpExchange(c net.Conn, query []byte) ([]byte, error) {
	if err := binary.Write(c, binary.BigEndian, uint16(len(query))); err != nil {
		return nil, err
	}
	if _, err := c.Write(query); err != nil {
		return nil, err
	}
	return readFrame(c)
}

func readFrame(r io.Reader) ([]byte, error) {
	var size uint16
	if err := binary.Read(r, binary.BigEndian, &size); err != nil {
		return nil, err
	}
	buf := make([]byte, size)
	_, err := io.ReadFull(r, buf)
	return buf, err
}

func main() {
	if len(os.Args) != 2 {
		os.Exit(2)
	}
	addr := "127.0.0.1:53"
	udp, err := net.ListenPacket("udp", addr)
	if err != nil {
		fmt.Println("KOBO_DNS_FORWARD=FAIL", err)
		os.Exit(1)
	}
	tcp, err := net.Listen("tcp", addr)
	if err != nil {
		fmt.Println("KOBO_DNS_FORWARD=FAIL", err)
		os.Exit(1)
	}
	fmt.Println("KOBO_DNS_FORWARD=READY")
	go func() {
		buf := make([]byte, 4096)
		for {
			n, peer, err := udp.ReadFrom(buf)
			if err != nil {
				return
			}
			query := append([]byte(nil), buf[:n]...)
			go func() {
				if reply := exchange("udp", query); reply != nil {
					udp.WriteTo(reply, peer)
				}
			}()
		}
	}()
	go func() {
		for {
			c, err := tcp.Accept()
			if err != nil {
				return
			}
			go func() {
				defer c.Close()
				c.SetDeadline(time.Now().Add(10 * time.Second))
				query, err := readFrame(c)
				if err != nil {
					return
				}
				if reply := exchange("tcp", query); reply != nil {
					binary.Write(c, binary.BigEndian, uint16(len(reply)))
					c.Write(reply)
				}
			}()
		}
	}()
	for {
		if _, err := os.Stat(os.Args[1]); err == nil {
			return
		}
		time.Sleep(time.Second)
	}
}
