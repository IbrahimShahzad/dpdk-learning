#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_eal.h>
#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_mbuf.h>

static volatile bool force_quit;

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define BURST_SIZE 32

#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

static u_int8_t forwarding_lcore = 1;
static u_int8_t mac_swap = 1;

static void
print_stats(void){
  struct rte_eth_stats stats;
  u_int8_t nb_ports = rte_eth_dev_count();
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

static void
simple_mac_swap(struct rte_mbuf **bufs,uint16_t nb_mbufs){
  struct ether_hdr *eth;
  struct ether_addr tmp;
  struct rte_mbuf *m;
  u_int16_t buf;

  for (buf=0;buf<nb_mbufs;buf++){
    m = bufs[buf];
    eth = rte_pktmbuf_mtod(m,struct ether_hdr *);
    ether_addr_copy(&eth->s_addr,&tmp);
    ether_addr_copy(&eth->d_addr,&eth->s_addr);
    ether_addr_copy(&tmp,&eth->d_addr);
  }

}

int lcore_main(void *arg){
  unsigned int lcore_id = rte_lcore_id();
  const u_int8_t nb_ports = rte_eth_dev_count();
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

      if (mac_swap)
        simple_mac_swap(bufs,nb_rx);

      /* send burst of Tx packets to the 
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
    .rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
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
  nb_ports = rte_eth_dev_count();
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
  rte_eal_mp_remote_launch(lcore_main,NULL,SKIP_MASTER);
  rte_eal_mp_wait_lcore();

  

  return 0;
}
