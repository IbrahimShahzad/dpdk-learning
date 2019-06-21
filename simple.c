#include <stdio.h>
#include <stdlib.h>

#include <rte_eal.h>
#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_mbuf.h>

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250


port_init(u_int8_t port,struct rte_mempool *mbuf_pool){
  return 0;
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
  

  return 0;
}
