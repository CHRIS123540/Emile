#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_timer.h>
#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_mbuf_dyn.h>
#include <rte_malloc.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define RING_BUF_SIZE 65536
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define FLOW_NUM 2000

#define PRINT_IP_ADDR(ip_addr) printf("IP: %d.%d.%d.%d\n", \
    (int)((ip_addr) >> 24) & 0xFF, \
    (int)((ip_addr) >> 16) & 0xFF, \
    (int)((ip_addr) >> 8) & 0xFF, \
    (int)(ip_addr) & 0xFF)

#define PRINT_PORT_MAC(ether_addr) \
    printf("MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 \
           " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n", \
		    RTE_ETHER_ADDR_BYTES(&ether_addr))

#define PRINT_PORT(port)\
	printf("PORT: %" PRIu16 "\n", port);

#define PRINT(ipv4_tuple,eth_hdr)\
	PRINT_PORT_MAC(eth_hdr->src_addr);\
	PRINT_PORT_MAC(eth_hdr->dst_addr);\
	PRINT_IP_ADDR(ipv4_tuple.src_addr);\
	PRINT_IP_ADDR(ipv4_tuple.dst_addr);\
	PRINT_PORT(ipv4_tuple.sport);\
	PRINT_PORT(ipv4_tuple.dport)	

static uint64_t Interval;
uint64_t total = 0;
uint64_t total_last = 0;

uint64_t total_detect = 0;
uint64_t total_detect_last = 0;
uint64_t Flow_num = 2000000;
struct rte_hash *flow_tables;
struct rte_ring *ring;
//作为特征桶


typedef struct flow
{
    int label;                      	/* 0: normal, 1: mining */
} flow;

flow *buckets;

typedef struct v4_tuple
{
	uint32_t src_addr;
	uint32_t dst_addr;
	union {
		struct {
			uint16_t sport;
			uint16_t dport;
		};
		uint32_t ports;
	};
} v4_tuple;

static struct rte_hash *create_hash_table(const char *tablename, uint32_t entry_num)
{
	struct rte_hash *hash_table;
	struct rte_hash_parameters hash_params = {0};

	hash_params.entries = entry_num;
	hash_params.key_len = sizeof(v4_tuple);
	hash_params.hash_func = rte_jhash;
	hash_params.hash_func_init_val = 0;

	hash_params.name = tablename;
	hash_params.socket_id = rte_socket_id();
	hash_params.extra_flag = RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY;
	// hash_params.key_mode = RTE_HASH_KEY_MODE_DUP;

	/* Find if the hash table was created before */
	hash_table = rte_hash_find_existing(hash_params.name);
	if (hash_table != NULL)
	{
		printf("exist\n");
		return hash_table;
	}
	else
	{
		hash_table = rte_hash_create(&hash_params);
		if (!hash_table)
		{
			printf("creat failed\n");
			exit(0);
		}
	}

	return hash_table;
}


static const struct rte_eth_conf port_conf_default = {
	.rxmode = {
		.mq_mode = RTE_ETH_MQ_RX_RSS,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_hf = RTE_ETH_RSS_IP | RTE_ETH_RSS_UDP |
				RTE_ETH_RSS_TCP,
		}
	},
};

//启动端口
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;
	
	PRINT_PORT_MAC(addr);

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}

static int
lcore_detect(__rte_unused void *arg)
{
	uint16_t port = rte_lcore_id();
	printf("port DETECT%d\n\n\n\n\n",port);
    struct rte_ipv4_hdr *ipv4;
    struct rte_tcp_hdr *tcp;
    struct rte_udp_hdr *udp;
	v4_tuple ipv4_tuple;
	struct rte_ether_hdr *eth_hdr;                

	flow *CV[10] ;


	/* Main work of application loop. 8< */
	while(1){
		//rte_delay_ms(100);
		int NUM = rte_ring_sc_dequeue_burst(ring, (void **)CV, 10, NULL);
		if(NUM > 0){
			
			for(int i =0;i < NUM; i++)
			{
//				printf("CV %d\n",CV[i]->label);
				total_detect = total_detect + 1;
			//printf("CV %d\n",*CV[0]);			
			//printf("core 2\n");
			}
		}
	}
	/* >8 End of loop. */
	return 0;
}

static uint32_t hash_match(struct rte_hash *flow_tables, v4_tuple ipv4_tuple)
{
	
	int flowid;
	int ret = rte_hash_lookup(flow_tables, (void const *)&ipv4_tuple);

	int C = 1;
	int *cv = &C;



		if (ret == -ENOENT)
		{
			flowid = rte_hash_add_key(flow_tables, (void const *)&ipv4_tuple);
//			printf("NEW TABLE %d\n",flowid);
		}
		else if (ret == -EINVAL)
		{
//			printf("Error!\n");
		}
		else
		{
			
			buckets[ret].label = ret;
			flow *cnn = &buckets[ret]; 
			int success = rte_ring_sp_enqueue_burst(ring, (void **)&cnn, 1, NULL);	
//			printf("success %d\n\n\n",success);
//			printf("MATCH Ret %d!\n",ret);
		}

}

static int
lcore_main(__rte_unused void *arg)
{
	uint16_t port = rte_lcore_id();
	printf("port %d\n\n\n\n\n",port);
    struct rte_ipv4_hdr *ipv4;
    struct rte_tcp_hdr *tcp;
    struct rte_udp_hdr *udp;
	v4_tuple ipv4_tuple;
	struct rte_ether_hdr *eth_hdr;                
	RTE_ETH_FOREACH_DEV(port)
		if (rte_eth_dev_socket_id(port) >= 0 &&
				rte_eth_dev_socket_id(port) !=
						(int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
					"polling thread.\n\tPerformance will "
					"not be optimal.\n", port);

	
	
	ring = rte_ring_create("Emile", RING_BUF_SIZE, rte_socket_id(), RING_F_MP_RTS_ENQ | RING_F_SC_DEQ);
    
	int C = 1;
	
	int *cv = &C;
	rte_ring_sp_enqueue_burst(ring, (void **)&cv, 1, NULL);	
    
	C = 2;
	cv = &C;
	rte_ring_sp_enqueue_burst(ring, (void **)&cv, 1, NULL);		
	
	printf("\nCore %u forwarding packets%d %d. [Ctrl+C to quit]\n",rte_lcore_id(),rte_ring_count(ring),rte_ring_get_capacity(ring));
	
	int *CV[2] ;
	rte_ring_sc_dequeue_burst(ring, (void **)CV, 2, NULL);
	printf("CV %d\n",*CV[0]);
	printf("CV %d\n",*CV[1]);

	
	/* Main work of application loop. 8< */
	while(1){
		RTE_ETH_FOREACH_DEV(port) {

			/* Get burst of RX packets, from first port of pair. */
			struct rte_mbuf *bufs[BURST_SIZE];
			const uint16_t nb_rx = rte_eth_rx_burst(port, 0,
					bufs, BURST_SIZE);
			
			if (unlikely(nb_rx == 0))
				continue;

			if(nb_rx > 0)
			{
			    total += nb_rx;				
			//	printf("Total %ld\n",total);
			}
			/*这一块用于释放收包*/
			uint16_t buf;
			int flowid;
			for (buf = 0; buf < nb_rx; buf++){


				uint32_t usr_value = bufs[buf]->hash.rss;
//            	printf("Packet usr value: %u\n", usr_value);

				ipv4 = (struct rte_ipv4_hdr *)(rte_pktmbuf_mtod(bufs[buf], struct rte_ether_hdr *) + 1);
                ipv4_tuple.src_addr = rte_be_to_cpu_32(ipv4->src_addr);
                ipv4_tuple.dst_addr = rte_be_to_cpu_32(ipv4->dst_addr);
				//eth_hdr = rte_pktmbuf_mtod(bufs[buf], struct rte_ether_hdr *);
			
				/* only process TCP flows */
                if (ipv4->next_proto_id == 6)
                {
                    tcp = (struct rte_tcp_hdr *)((unsigned char *)ipv4 + sizeof(struct rte_ipv4_hdr));
                    ipv4_tuple.sport = rte_be_to_cpu_16(tcp->src_port);
                    ipv4_tuple.dport = rte_be_to_cpu_16(tcp->dst_port);
					
					//源/目的mac->源/目的ip->源/目的port
//					PRINT(ipv4_tuple,eth_hdr);
//					printf("lenth %u\n",rte_pktmbuf_pkt_len(bufs[buf]));
					
					int ret = hash_match(flow_tables, ipv4_tuple);


                }
                else if (ipv4->next_proto_id == 17)
                {
                    udp = (struct rte_udp_hdr *)((unsigned char *)ipv4 + sizeof(struct rte_ipv4_hdr));
                    ipv4_tuple.sport = rte_be_to_cpu_16(udp->src_port);
                    ipv4_tuple.dport = rte_be_to_cpu_16(udp->dst_port);

                }	
				
				rte_pktmbuf_free(bufs[buf]);
				}

		}
	}
	/* >8 End of loop. */
	return 0;
}


static void stats_display()
{
	const char clr[] = {27, '[', '2', 'J', '\0'};
	const char top_left[] = {27, '[', '1', ';', '1', 'H', '\0'};
	int i, portid;

	/* Clear screen and move to top left */
	printf("%s%s", clr, top_left);
	printf("Total %lu\n",total - total_last);
	printf("Total_detect %lu\n",total_detect - total_detect_last);
	total_last = total;
	total_detect_last = total_detect;
}

// 定时器回调函数
static void timer_callback(struct rte_timer *timer, void *arg) {

	stats_display();

}

int
main(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	unsigned nb_ports;
	uint16_t portid;

	/* Initializion the Environment Abstraction Layer (EAL). 8< */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	/* >8 End of initialization the Environment Abstraction Layer (EAL). */

	argc -= ret;
	argv += ret;

	/* Check that there is an even number of ports to send/receive on. */
	nb_ports = rte_eth_dev_count_avail();
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	/* >8 End of allocating mempool to hold mbuf. */

	buckets = (flow *)rte_malloc("bucket", FLOW_NUM * sizeof(flow), RTE_CACHE_LINE_SIZE);

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initializing all ports. 8< */
	RTE_ETH_FOREACH_DEV(portid)
		if (port_init(portid, mbuf_pool) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
					portid);
	/* >8 End of initializing all ports. */

	/*创建哈希表*/
	char tbl_name[RTE_MEMPOOL_NAMESIZE];
	snprintf(tbl_name, RTE_MEMPOOL_NAMESIZE, "Emile");	
	flow_tables = create_hash_table(tbl_name, Flow_num);
	
	printf("\n%d lcores used.\n",rte_lcore_count());
	
	// 初始化定时器子系统
    rte_timer_subsystem_init();

    // 创建定时器
    struct rte_timer timer;
    rte_timer_init(&timer);

    // 设置定时器每秒触发一次
    uint64_t hz = rte_get_timer_hz();
	Interval = hz;
    rte_timer_reset(&timer, hz, PERIODICAL, rte_lcore_id(), timer_callback, NULL);

	rte_eal_remote_launch(lcore_main, NULL, 1);
	rte_eal_remote_launch(lcore_detect, NULL, 2);
/*此处预计放置打印的内容*/
    while (1) {
        const char *data = "1\n";
    //    write(pipe_fd, data, 2); // 写入数字“1”和换行符
        rte_timer_manage();
        rte_delay_us_block(10000);
    }

	rte_eal_mp_wait_lcore();
	rte_eal_cleanup();

	return 0;
}
