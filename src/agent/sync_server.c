/*
  +----------------------------------------------------------------------+
  | sync_agent                                                           |
  +----------------------------------------------------------------------+
  | this source file is subject to version 2.0 of the apache license,    |
  | that is bundled with this package in the file license, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.apache.org/licenses/license-2.0.html                      |
  | if you did not receive a copy of the apache2.0 license and are unable|
  | to obtain it through the world-wide-web, please send a note to       |
  | yiming_6weijun@163.com so we can mail you a copy immediately.        |
  +----------------------------------------------------------------------+
  | author: weijun lu  <yiming_6weijun@163.com>                          |
  +----------------------------------------------------------------------+
*/

#include "sync_agent.h"

/**
 * 全局变量定义区
 */
sync_server_t *_server_t = NULL;
int cpu_num = 1;

/**
 * 静态函数声明区
 */

/**
 *        Name: sync_config_free
 * Description: sync_server 监听程序 & client自动发现 & 接受同步文件请求
 *   Parameter: pass args
 *      Return:
 */
static void sync_server_listen(void *arg);

/**
 *        Name: server_accept_loop
 * Description: accept lthread
 *   Parameter: pass args
 *      Return:
 */
static void* server_accept_loop(void *args);

/**
 *        Name: client_connection_handler
 * Description: connection handler lthread
 *   Parameter: pass args
 *      Return:
 */
static void client_connection_handler(void *arg);

/**
 *        Name: sync_server_watch
 * Description: 监控对应的目录的文件变化
 *   Parameter: pass args
 *      Return:
 */
static void sync_server_watch(void *arg);

/**
 *        Name: set_free
 * Description: simple_set free
 *   Parameter: pass args
 *      Return:
 */
static void set_free(void *set);


int sync_server_init()
{
	cpu_num = sysconf(_SC_NPROCESSORS_CONF);
	if (cpu_num < 1) {
	    cpu_num = 1;
	}

	_server_t = (sync_server_t *)malloc(sizeof(sync_server_t));
	if ( NULL == _server_t ) {
		log_error("malloc sync_server_t error");
		return 0;
	}
	_server_t->path_ip_set = hashmap_new(HASHMAP_PATH_NODES, GH_COPYKEYS,
								GH_USERKEYS, set_free);
	if ( NULL == _server_t->path_ip_set ) {
		log_error("path_ip_set hashmap_new error");
		return 0;
	}

	return 1;
}

void sync_server_destroy()
{
	hashmap_delete(_server_t->path_ip_set);
	free(_server_t);
}

static void set_free(void *set)
{
	set_destroy((simple_set*)set);
}

static void client_connection_handler(void *arg)
{
	DEFINE_LTHREAD;
	lthread_detach();

	char *buf = NULL;
	int ret = 0;
	_connection_t *conn = (_connection_t*)arg;
	char *ip = inet_ntoa(conn->cli_addr.sin_addr);

	if ((buf = (char*)malloc(1024)) == NULL) {
		lthread_log(LOG_LEVEL_ERROR, "client_connection_handler malloc buf error");
		return;
	}
	bzero(buf, 1024);

	ret = lthread_recv(conn->fd, buf, 1024, 0, 2000);
	if (ret < 0) {
		goto destroy;
	}
	//1.接收健康检查curl
	//2.接收client的心跳和订阅信息
	//3.接收客户端的对账信息
	char *res = "HTTP/1.0 200 OK\r\nContent-length: 11\r\n\r\nsync_server";
	int snd_ret = lthread_send(conn->fd, res, strlen(res), 0);

destroy:
	lthread_close(conn->fd);

	free(buf);
	free(conn);
}

static void sync_server_listen( void *arg)
{
	int listen_fd = *(int*)arg;
	int cli_fd = 0;
	struct sockaddr_in cli_addr = {};
	socklen_t addr_len = sizeof(cli_addr);
	lthread_t *cli_lt = NULL;

	DEFINE_LTHREAD;
	lthread_detach();

	while (_main_continue) {
		cli_fd = lthread_accept(listen_fd, (struct sockaddr*)&cli_addr, &addr_len);
		if (cli_fd == -1) {
			lthread_log(LOG_LEVEL_ERROR, "sync_server lthread_accept error, cli_fd[%d]", cli_fd);
			return;
		}

		_connection_t *cli_conn = (_connection_t*)malloc(sizeof(_connection_t));
		cli_conn->cli_addr = cli_addr;
		cli_conn->fd       = cli_fd;

		lthread_t *handler_t = NULL;
		lthread_create(&handler_t, client_connection_handler, cli_conn);
	}
}

static void* server_accept_loop(void *args)
{
	lthread_t *listen_t = NULL;
	lthread_create(&listen_t, sync_server_listen, args);
	lthread_run();

	return 0;
}

void sync_server_start()
{
	pthread_t pths[cpu_num];
	int i=0;

	int listen_fd = 0;
	int opt       = 1;
	int ret       = 0;
	struct sockaddr_in sin = {};

	listen_fd = lthread_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_fd == -1) {
		log_error("lthread_socket create error. listen_fd[%d]", listen_fd);
		return;
	}
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt ,sizeof(int)) == -1) {
		log_error("setsockopt error:set reuseaddr error");
		return;
	}

	sin.sin_family      = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port        = htons(_config->port);

	ret = bind(listen_fd, (struct sockaddr*)&sin, sizeof(sin));
	if ( ret == -1) {
		log_error("bind error port[%d]", _config->port);
		return;
	}

	listen(listen_fd, 1024);
	log_debug("sync_server started,listen on port[%d]", _config->port);

	while (i++ < cpu_num) {
		pthread_create(&pths[i], NULL, server_accept_loop, &listen_fd);
	}

	i=0;
	while ( i++ < cpu_num ) {
		pthread_join(pths[i], NULL);
	}
}

/**
 * 监测对应的目录的文件变化：
 * 1.inotify监测文件夹&文件变化
 * 2.写文件列表？
 * 3.对账如何对？
 * 4.如何并发？pull or push
 */
static void sync_server_watch(void *arg)
{

}
