#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_eal.h>
#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <rte_tcp.h>
#include <rte_ip.h>
#include <rte_ether.h>

static volatile bool force_quit;

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define BURST_SIZE 32

#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

#define IPV4_PROTO 2048
#define IPV6_PROTO 34525
static u_int8_t forwarding_lcore = 1;
static u_int8_t mac_swap = 1;


u_int16_t get_Ether_Type(char *pointer)
{
    u_int8_t slb = 0;
    u_int8_t lb = 0;
    slb= *(pointer - 2);// second last byte of ETH layer
    lb= *(pointer - 1);// last last byte of ETH layer
    return (slb* 256) +lb;
}
bool is_ipv4(u_int16_t val)
{
  if (val == IPV4_PROTO)
    return true;
  return false;
}

static inline int is_valid_ipv4_pkt(struct rte_ipv4_hdr *pkt, uint32_t link_len)
{
    /* From http://www.rfc-editor.org/rfc/rfc1812.txt section 5.2.2 */
    /*
     * 1. The packet length reported by the Link Layer must be large
     * enough to hold the minimum length legal IP datagram (20 bytes).
     */
    if (link_len < sizeof(struct rte_ipv4_hdr))
        return -1;
    /* 2. The IP checksum must be correct. */
    /* this is checked in H/W */
    /*
     * 3. The IP version number must be 4. If the version number is not 4
     * then the packet may be another version of IP, such as IPng or
     * ST-II.
     */
    if (((pkt->version_ihl) >> 4) != 4)
        return -3;
    /*
     * 4. The IP header length field must be large enough to hold the
     * minimum length legal IP datagram (20 bytes = 5 words).
     */
    if ((pkt->version_ihl & 0xf) < 5)
        return -4;
    /*
     * 5. The IP total length field must be large enough to hold the IP
     * datagram header, whose length is specified in the IP header length
     * field.
     */
    if (rte_cpu_to_be_16(pkt->total_length) < sizeof(struct rte_ipv4_hdr))
        return -5;
    return 0;
}

static void
print_stats(void){
  struct rte_eth_stats stats;
  u_int8_t nb_ports = rte_eth_dev_count_avail();
  u_int8_t port;

  for(port=0;port<nb_ports;port++){
    printf("\nStatistics for the port %u\n",port);
    rte_eth_stats_get(port,&stats);
    printf("RX:%911u Tx:%911u dropped:%911u\n",
        stats.ipackets,stats.opackets,stats.imissed);
  }
}

static int
check_link_status(u_int16_t nb_ports){
  struct rte_eth_link link;
  u_int8_t port;
  for (port=0;port<nb_ports;port++){
    rte_eth_link_get(port,&link);
    if(link.link_status == ETH_LINK_DOWN){
      RTE_LOG(INFO,APP,"Port: %u Link DOWN\n",port);
      return -1;
    }
    RTE_LOG(INFO,APP,"Port: %u Link UP Speed %u\n",
        port,link.link_speed);
  }
}



int worker_main(void *arg){
  unsigned int lcore_id = rte_lcore_id();
  const u_int8_t nb_ports = rte_eth_dev_count_avail();
  u_int8_t port;
  u_int8_t dest_port;

  if (lcore_id != forwarding_lcore){
    RTE_LOG(INFO,APP,"lcore %u exiting\n",lcore_id);
    return 0;
  }

  /* Run until app is killed or quit */
  while(!force_quit){
    /* Receive packets on port
     * and forward them to  a paired port
     * the mapping is 0->1,1->0 , 2-3
     * */
	  for(port=0;port< nb_ports;port++){
		  struct rte_mbuf *bufs[BURST_SIZE];
		  u_int16_t nb_rx;
		  u_int16_t nb_tx;
		  u_int16_t buf;

		  /* Get burst fo RX packets */
		  nb_rx = rte_eth_rx_burst(port,0,
				  bufs,BURST_SIZE);
		  if (unlikely(nb_rx==0))
			  continue;

		  for(int i=0;i<nb_rx;i++){
			  /* Write Code here */
			  struct rte_ether_hdr *ethernet_header;
			  u_int8_t source_mac_address[RTE_ETHER_ADDR_LEN];
			  u_int8_t destination_mac_address[RTE_ETHER_ADDR_LEN];
			  struct rte_ipv4_hdr     *pIP4Hdr;
			  struct rte_ipv6_hdr     *pIP6Hdr;
			  struct rte_udp_hdr      *pUdpHdr;
			  struct rte_tcp_hdr      *pTcpHdr;
			  u_int32_t u32SrcIPv4= 0;
			  u_int32_t u32DstIPv4= 0;
			  u_int16_t u16SrcPort= 0;
			  u_int16_t u16DstPort= 0;

              u_int16_t ethernet_type;
			  ethernet_header = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr *); 
			  ethernet_type = ethernet_header->ether_type;
			  rte_memcpy(source_mac_address,&ethernet_header->s_addr,sizeof(u_int8_t)*RTE_ETHER_ADDR_LEN);
			  rte_memcpy(destination_mac_address,&ethernet_header->d_addr,sizeof(u_int8_t)*RTE_ETHER_ADDR_LEN);
			  RTE_LOG(INFO,APP,"\nSource Mac: ");
			  for(int i=0;i<RTE_ETHER_ADDR_LEN;i++)
				  RTE_LOG(INFO,APP,"%x",source_mac_address[i]);
			  RTE_LOG(INFO,APP,"\nDestination Mac: ");
			  for(int i=0;i<RTE_ETHER_ADDR_LEN;i++)
				  RTE_LOG(INFO,APP,"%x",source_mac_address[i]);
			  RTE_LOG(INFO,APP,"\n");
			  RTE_LOG(INFO,APP,"ether type: %d",ethernet_type);
			  void* pHdrTraverse = (void*) ((unsigned char*) ethernet_header + sizeof (struct rte_ether_hdr));// Pointer to Next Layer to Eth
			  u_int16_t next_proto = get_Ether_Type(pHdrTraverse);// holds last two byte value of ETH Layer
			  RTE_LOG(INFO,APP,"next proto %u\n",next_proto);
			  if (is_ipv4(next_proto)){
				  pIP4Hdr = rte_pktmbuf_mtod_offset(bufs[i], struct rte_ipv4_hdr*, sizeof (struct rte_ether_hdr));
				  /* check for valid packet */
				  if (is_valid_ipv4_pkt(pIP4Hdr,bufs[i]->pkt_len)>=0){
					  /* update TTL and CHKSM */
					  --(pIP4Hdr->time_to_live);
					  ++(pIP4Hdr->hdr_checksum);

					  u32SrcIPv4 = rte_bswap32(pIP4Hdr->src_addr);
					  u32DstIPv4 = rte_bswap32(pIP4Hdr->dst_addr);
					  RTE_LOG(INFO,APP,"IPv4 src %u dst %u\n",u32SrcIPv4,u32DstIPv4);

					  switch(pIP4Hdr->next_proto_id){

						  case IPPROTO_TCP:
							  pTcpHdr = (struct rte_tcp_hdr *) ((unsigned char *) pIP4Hdr + sizeof(struct rte_ipv4_hdr));
							  u16DstPort = rte_bswap16(pTcpHdr->dst_port);
							  u16SrcPort = rte_bswap16(pTcpHdr->src_port);
							  break;

						  case IPPROTO_UDP:
							  pUdpHdr = (struct rte_udp_hdr *) ((unsigned char *) pIP4Hdr + sizeof (struct rte_ipv4_hdr));
							  u16DstPort = rte_bswap16(pUdpHdr->dst_port);
							  u16SrcPort = rte_bswap16(pUdpHdr->src_port); 
							  break;

						  default:
							  u16DstPort = 0;
							  u16SrcPort = 0;
							  break;

					  }
					  RTE_LOG(INFO,APP,"TL src %u dst %u\n",u16SrcPort,u16DstPort);
				  }

			  }
        }
      	/* send burst of Tx packets to the
      	 *
      	 * second port
      	 */
      	dest_port = port ^ 1;
      	nb_tx= rte_eth_tx_burst(dest_port, 0,
          	bufs, nb_rx);
      	/* Free any unsent packets. */
      	if(unlikely(nb_rx < nb_rx)){
        	for (buf=0;buf<nb_rx;buf++)
          		rte_pktmbuf_free(bufs[buf]);
        }
    }
  }
  return 0;
}

static inline int
port_init(u_int8_t port,struct rte_mempool *mbuf_pool){
  struct rte_eth_conf port_conf = {
    .rxmode = { .max_rx_pkt_len = RTE_ETHER_MAX_LEN }
  };
  const u_int16_t nb_rx_queues = 1;
  const u_int16_t nb_tx_queues = 1;
  int ret;
  u_int16_t q;

  /* configure the ethernet device */
  ret = rte_eth_dev_configure(port,
      nb_rx_queues,
      nb_tx_queues,
      &port_conf);
  if (ret != 0)
    return ret;

  /* Allocate and setup 1 RX queue per Ethernet port */
  for (q=0;q<nb_rx_queues;q++){
    ret=rte_eth_rx_queue_setup(port,q,RX_RING_SIZE,
        rte_eth_dev_socket_id(port),
        NULL, mbuf_pool);
    if (ret<0)
      return ret;
  }

  /* Allocate and setup 1 RX queue per Ethernet port */
  for (q=0;q<nb_tx_queues;q++){
  ret=rte_eth_tx_queue_setup(port,q,TX_RING_SIZE,
      rte_eth_dev_socket_id(port),
      NULL);
  if (ret<0)
    return ret;
  }

  /* start the ethernet port */
  ret = rte_eth_dev_start(port);
  if (ret<0)
    return ret;


  /* Enable RX in promiscuous mode for the Ethernet device */
  rte_eth_promiscuous_enable(port);


  return 0;
}

static void
signal_handler(int signum){
  if(signum== SIGINT || signum== SIGTERM){
    printf("\n\nSignal %d received,preparing to exit...\n",signum);
    force_quit = true;
    print_stats();
  }
}

int main(int argc, char* argv[]){
  printf("\n");

  int ret;
  u_int8_t nb_ports;
  struct rte_mempool *mbuf_pool;
  u_int8_t portid;

  /*
   * EAL: "Environment Abstraction Layer"
   * EAL gets parameters from cli, returns number of parsed args
   * cpu_init: fill cpu_info structure
   * log_init
   * config_init: create memory configuration in shared memory
   * pci_init: scan pci bus
   * memory_init (hugepages)
   * memzone_init: initialize memzone subsystem
   * alarm_init: for timer interrupts
   * timer_init
   * plugin init
   * dev_init: initialize and probe virtual devices
   * intr_init: create na interrupt handler thread
   * lcore_init: Create a thread per lcore
   * pci_probe: probel all physical devices
   */
  ret = rte_eal_init(argc,argv);
  if (ret<0)
    rte_exit(EXIT_FAILURE,"EAL Init failed\n");

  argc -= ret;
  argv += ret;

  force_quit = false;
  signal(SIGINT,signal_handler);
  signal(SIGTERM,signal_handler);


  /*
   * Check there is an even number of ports to
   * send and receive on
   */
  nb_ports = rte_eth_dev_count_avail();
  if(nb_ports < 2 || (nb_ports & 1)){
    rte_exit(EXIT_FAILURE,"Invalid port number\n");
  }

  RTE_LOG(INFO, APP, "Number of ports:%u\n",nb_ports);


  /* Create a new mbuf mempool */
  mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL",
      NUM_MBUFS *nb_ports,
      MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
      rte_socket_id());

  if (mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE,"mbuff_pool create failed\n");


  /* Initialize all ports */
  for (portid = 0; portid < nb_ports; portid++){
    if(port_init(portid,mbuf_pool) != 0)
      rte_exit(EXIT_FAILURE,"port init failed\n");
  }

  if(mac_swap)
    RTE_LOG(INFO,APP,"MAC address swapping enabledi\n");

  ret=check_link_status(nb_ports);
  if (ret<0){
    RTE_LOG(WARNING,APP,"Some ports are down\n");
  }
  rte_eal_mp_remote_launch(worker_main,NULL,SKIP_MAIN);
  rte_eal_mp_wait_lcore();



  return 0;
}
