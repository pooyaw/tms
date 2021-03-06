#include <signal.h>

#include <iostream>

#include "any.h"
#include "base_64.h"
#include "bit_buffer.h"
#include "bit_stream.h"
#include "epoller.h"
#include "local_stream_center.h"
#include "protocol_factory.h"
#include "ref_ptr.h"
#include "socket_util.h"
#include "srt_epoller.h"
#include "srt_socket_util.h"
#include "srt_socket.h"
#include "ssl_socket.h"
#include "tcp_socket.h"
#include "timer_in_second.h"
#include "timer_in_millsecond.h"
#include "udp_socket.h"
#include "util.h"

#include "openssl/ssl.h"

static void sighandler(int sig_no)
{
    std::cout << LMSG << "sig:" << sig_no << std::endl;
	exit(0);
} 

LocalStreamCenter               g_local_stream_center;
Epoller*                        g_epoll = NULL;
SSL_CTX*                        g_tls_ctx = NULL;
SSL_CTX*                        g_dtls_ctx = NULL;
std::string                     g_dtls_fingerprint = "";
std::string                     g_local_ice_pwd = "";
std::string                     g_local_ice_ufrag = "";
std::string                     g_remote_ice_pwd = "";
std::string                     g_remote_ice_ufrag = "";
std::string                     g_server_ip = "";

void AvLogCallback(void* ptr, int level, const char* fmt, va_list vl)
{
    UNUSED(ptr);
    UNUSED(level);

    vprintf(fmt, vl);
}

int main(int argc, char* argv[])
{
	std::string server_crt = Util::ReadFile("server.crt");
	std::string server_key = Util::ReadFile("server.key");

    if (server_crt.empty() || server_key.empty())
    {
        std::cout << LMSG << "server.crt or server.key incorrect" << std::endl;
        return -1;
    }

    // Open ssl init
	SSL_load_error_strings();
    int ret = SSL_library_init();

    assert(ret == 1);

    // tls init
    g_tls_ctx = SSL_CTX_new(SSLv23_method());

    assert(g_tls_ctx != NULL);

	BIO *tls_mem_cert = BIO_new_mem_buf((void *)server_crt.c_str(), server_crt.length());
    X509 *tls_cert= PEM_read_bio_X509(tls_mem_cert,NULL,NULL,NULL);
    SSL_CTX_use_certificate(g_tls_ctx, tls_cert);
    X509_free(tls_cert);
    BIO_free(tls_mem_cert);    
    
    BIO *tls_mem_key = BIO_new_mem_buf((void *)server_key.c_str(), server_key.length());
    RSA *tls_rsa_private = PEM_read_bio_RSAPrivateKey(tls_mem_key, NULL, NULL, NULL);
    SSL_CTX_use_RSAPrivateKey(g_tls_ctx, tls_rsa_private);
    RSA_free(tls_rsa_private);
    BIO_free(tls_mem_key);

    ret = SSL_CTX_check_private_key(g_tls_ctx);

    // dtls init
    //g_dtls_ctx = SSL_CTX_new(DTLSv1_2_method());

#if 0 
    // 用导入证书的方法,目前不可行
    g_dtls_ctx = SSL_CTX_new(DTLSv1_method());
	BIO *dtls_mem_cert = BIO_new_mem_buf((void *)server_crt.c_str(), server_crt.length());
    X509 *dtls_cert= PEM_read_bio_X509(dtls_mem_cert,NULL,NULL,NULL);
    SSL_CTX_use_certificate(g_dtls_ctx, dtls_cert);
    X509_free(dtls_cert);
    BIO_free(dtls_mem_cert);    
    
    BIO *dtls_mem_key = BIO_new_mem_buf((void *)server_key.c_str(), server_key.length());
    RSA *dtls_rsa_private = PEM_read_bio_RSAPrivateKey(dtls_mem_key, NULL, NULL, NULL);
    SSL_CTX_use_RSAPrivateKey(g_dtls_ctx, dtls_rsa_private);
    RSA_free(dtls_rsa_private);
    BIO_free(dtls_mem_key);

    ret = SSL_CTX_check_private_key(g_dtls_ctx);
#else
    // 代码里生成证书
	EVP_PKEY* dtls_private_key = EVP_PKEY_new();
    if (dtls_private_key == NULL)
    {
        std::cout << LMSG << "EVP_PKEY_new err" << std::endl;
        return -1;
    }

    RSA* rsa = RSA_new();
    if (rsa == NULL)
    {
        std::cout << LMSG << "RSA_new err" << std::endl;
        return -1;
    }

    BIGNUM* exponent = BN_new();
    if (exponent == NULL)
    {
        std::cout << LMSG << "BN_new err" << std::endl;
        return -1;
    }

    BN_set_word(exponent, RSA_F4);

	const std::string& aor = "www.john.com";
	int expire_day = 365;
	int private_key_len = 1024;

    RSA_generate_key_ex(rsa, private_key_len, exponent, NULL);

    ret = EVP_PKEY_set1_RSA(dtls_private_key, rsa);
    if (ret != 1)
    {
        std::cout << LMSG << "EVP_PKEY_set1_RSA err:" << ret << std::endl;
    }

    X509* dtls_cert = X509_new();
    if (dtls_cert == NULL)
    {
        std::cout << LMSG << "X509_new err" << std::endl;
        return -1;
    }

    X509_NAME* subject = X509_NAME_new();
    if (subject == NULL)
    {
        std::cout << LMSG << "X509_NAME_new err" << std::endl;
        return -1;
    }

    int serial = rand();
    ASN1_INTEGER_set(X509_get_serialNumber(dtls_cert), serial);

    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, (unsigned char *) aor.data(), aor.size(), -1, 0);

    X509_set_issuer_name(dtls_cert, subject);
    X509_set_subject_name(dtls_cert, subject);

    const long cert_duration = 60*60*24*expire_day;

    X509_gmtime_adj(X509_get_notBefore(dtls_cert), 0);
    X509_gmtime_adj(X509_get_notAfter(dtls_cert), cert_duration);

    ret = X509_set_pubkey(dtls_cert, dtls_private_key);
    if (ret != 1)
    {
        std::cout << LMSG << "X509_set_pubkey err:" << ret << std::endl;
    }

    ret = X509_sign(dtls_cert, dtls_private_key, EVP_sha1());
    if (ret == 0)
    {
        std::cout << LMSG << "X509_sign err:" << ret << std::endl;
    }

    // free
    RSA_free(rsa);
    BN_free(exponent);
    X509_NAME_free(subject);

    g_dtls_ctx = SSL_CTX_new(DTLSv1_2_method());
	ret = SSL_CTX_use_certificate(g_dtls_ctx, dtls_cert);
    if (ret != 1)
    {   
        std::cout << LMSG << "|SSL_CTX_use_certificate error:" << ret << std::endl; 
    }   

    ret = SSL_CTX_use_PrivateKey(g_dtls_ctx, dtls_private_key);
    if (ret != 1)
    {   
        std::cout << LMSG << "|SSL_CTX_use_PrivateKey error:" << ret << std::endl; 
    }   

    ret = SSL_CTX_set_cipher_list(g_dtls_ctx, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
    if (ret != 1)
    {   
        std::cout << LMSG << "|SSL_CTX_set_cipher_list error:" << ret << std::endl; 
    }   

    ret = SSL_CTX_set_tlsext_use_srtp(g_dtls_ctx, "SRTP_AES128_CM_SHA1_80");
    if (ret != 0)
    {   
        std::cout << LMSG << "|SSL_CTX_set_tlsext_use_srtp error:" << ret << std::endl; 
    }   

    SSL_CTX_set_verify_depth (g_dtls_ctx, 4); 
    SSL_CTX_set_read_ahead(g_dtls_ctx, 1);
#endif

    // dtls fingerprint
    char fp[100];
    char *fingerprint = fp;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int i; 
    unsigned int n; 
 
    int r = X509_digest(dtls_cert, EVP_sha256(), md, &n);
    
    for (i = 0; i < n; i++)
    {
        sprintf(fingerprint, "%02X", md[i]);
        fingerprint += 2;
        
        if(i < (n-1))
        {
            *fingerprint = ':';
        }
        else
        {
            *fingerprint = '\0';
        }
        ++fingerprint;
    }

    g_dtls_fingerprint.assign(fp, strlen(fp));

    std::cout << "DTLS fingerprint[" << g_dtls_fingerprint << "]" << std::endl;

    // parse args
    std::map<std::string, std::string> args_map = Util::ParseArgs(argc, argv);

    uint16_t rtmp_port              = 1935;
    uint16_t https_file_port        = 8643;
    uint16_t http_file_port         = 8666;
    uint16_t https_flv_port         = 8743;
    uint16_t http_flv_port          = 8787;
    uint16_t https_hls_port         = 8843;
    uint16_t http_hls_port          = 8888;
    uint16_t web_socket_port        = 8901;
    uint16_t ssl_web_socket_port    = 8943;
    uint16_t srt_port               = 9000;
    uint16_t webrtc_port            = 11445;

    bool daemon                     = false;

    auto iter_server_ip     = args_map.find("server_ip");
    auto iter_rtmp_port     = args_map.find("rtmp_port");
    auto iter_http_flv_port = args_map.find("http_flv_port");
    auto iter_http_hls_port = args_map.find("http_hls_port");
    auto iter_daemon        = args_map.find("daemon");

    if (iter_server_ip == args_map.end())
    {
        std::cout << "Usage:" << argv[0] << " -server_ip <xxx.xxx.xxx.xxx> -http_flv_port [xxx] -http_hls_port [xxx] -daemon [xxx]" << std::endl;
        return 0;
    }

    g_server_ip = iter_server_ip->second;

    if (iter_rtmp_port != args_map.end())
    {
        if (! iter_rtmp_port->second.empty())
        {
            rtmp_port = Util::Str2Num<uint16_t>(iter_rtmp_port->second);
        }
    }

    if (iter_http_flv_port != args_map.end())
    {
        if (! iter_http_flv_port->second.empty())
        {
            http_flv_port = Util::Str2Num<uint16_t>(iter_http_flv_port->second);
        }
    }

    if (iter_http_hls_port != args_map.end())
    {
        if (! iter_http_hls_port->second.empty())
        {
            http_hls_port = Util::Str2Num<uint16_t>(iter_http_hls_port->second);
        }
    }

    if (iter_daemon != args_map.end())
    {
        int tmp = Util::Str2Num<int>(iter_daemon->second);

        daemon = (! (tmp == 0));
    }

    if (daemon)
    {
        Util::Daemon();
    }

	signal(SIGUSR1, sighandler);
    signal(SIGPIPE,SIG_IGN);

    Log::SetLogLevel(kLevelDebug);

    DEBUG << argv[0] << " starting..." << std::endl;

    Epoller epoller;
    epoller.Create();
    g_epoll = &epoller;

    std::string local_ip = "";
    uint16_t local_port = 0;

    // === Init Timer ===
    TimerInSecond timer_in_second(&epoller);
    TimerInMillSecond timer_in_millsecond(&epoller);

    // === Init Server Rtmp Socket ===
    int server_rtmp_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(server_rtmp_fd);
    if (socket_util::Bind(server_rtmp_fd, "0.0.0.0", rtmp_port) != 0)
    {
        std::cout << LMSG << "bind rtmp_port " << rtmp_port << " error" << std::endl;
        return -1;
    }
    if (socket_util::Listen(server_rtmp_fd) != 0)
    {
        std::cout << LMSG << "listen rtmp_port " << rtmp_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(server_rtmp_fd);
    socket_util::GetSocketName(server_rtmp_fd, local_ip, local_port);

    TcpSocket server_rtmp_socket(&epoller, server_rtmp_fd, std::bind(&ProtocolFactory::GenRtmpProtocol, std::placeholders::_1, std::placeholders::_2));
    server_rtmp_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    server_rtmp_socket.EnableRead();
    server_rtmp_socket.AsServerSocket();

    // === Init Server Http Flv Socket ===
    int server_http_flv_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(server_http_flv_fd);
    if (socket_util::Bind(server_http_flv_fd, "0.0.0.0", http_flv_port) != 0)
    {
        std::cout << LMSG << "bind http_flv_port " << http_flv_port << " error" << std::endl;
        return -1;
    }
    if (socket_util::Listen(server_http_flv_fd) != 0)
    {
        std::cout << LMSG << "bind http_flv_port " << http_flv_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(server_http_flv_fd);
    socket_util::GetSocketName(server_http_flv_fd, local_ip, local_port);

    TcpSocket server_http_flv_socket(&epoller, server_http_flv_fd, std::bind(&ProtocolFactory::GenHttpFlvProtocol, std::placeholders::_1, std::placeholders::_2));
    server_http_flv_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    server_http_flv_socket.EnableRead();
    server_http_flv_socket.AsServerSocket();

    // === Init Server Https Flv Socket ===
    int server_https_flv_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(server_https_flv_fd);
    if (socket_util::Bind(server_https_flv_fd, "0.0.0.0", https_flv_port) != 0)
    {
        std::cout << LMSG << "bind https_flv_port " << https_flv_port << " error" << std::endl;
        return -1;
    }
    if (socket_util::Listen(server_https_flv_fd) != 0)
    {
        std::cout << LMSG << "bind https_flv_port " << https_flv_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(server_https_flv_fd);
    socket_util::GetSocketName(server_https_flv_fd, local_ip, local_port);

    SslSocket server_https_flv_socket(&epoller, server_https_flv_fd, std::bind(&ProtocolFactory::GenHttpFlvProtocol, std::placeholders::_1, std::placeholders::_2));
    server_https_flv_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    server_https_flv_socket.EnableRead();
    server_https_flv_socket.AsServerSocket();

    // === Init Server Http Hls Socket ===
    int server_http_hls_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(server_http_hls_fd);
    if (socket_util::Bind(server_http_hls_fd, "0.0.0.0", http_hls_port) != 0)
    {
        std::cout << LMSG << "bind http_hls_port " << http_hls_port << " error" << std::endl;
        return -1;
    }
    if (socket_util::Listen(server_http_hls_fd) != 0)
    {
        std::cout << LMSG << "bind http_hls_port " << http_hls_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(server_http_hls_fd);
    socket_util::GetSocketName(server_http_hls_fd, local_ip, local_port);

    TcpSocket server_http_hls_socket(&epoller, server_http_hls_fd, std::bind(&ProtocolFactory::GenHttpHlsProtocol, std::placeholders::_1, std::placeholders::_2));
    server_http_hls_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    server_http_hls_socket.EnableRead();
    server_http_hls_socket.AsServerSocket();

    // === Init Server Https Hls Socket ===
    int server_https_hls_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(server_https_hls_fd);
    if (socket_util::Bind(server_https_hls_fd, "0.0.0.0", https_hls_port) != 0)
    {
        std::cout << LMSG << "bind https_hls_port " << https_hls_port << " error" << std::endl;
        return -1;
    }
    if (socket_util::Listen(server_https_hls_fd) != 0)
    {
        std::cout << LMSG << "bind https_hls_port " << https_hls_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(server_https_hls_fd);
    socket_util::GetSocketName(server_https_hls_fd, local_ip, local_port);

    SslSocket server_https_hls_socket(&epoller, server_https_hls_fd, std::bind(&ProtocolFactory::GenHttpHlsProtocol, std::placeholders::_1, std::placeholders::_2));
    server_https_hls_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    server_https_hls_socket.EnableRead();
    server_https_hls_socket.AsServerSocket();

    // === Init WebSocket Socket ===
    int web_socket_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(web_socket_fd);
    if (socket_util::Bind(web_socket_fd, "0.0.0.0", web_socket_port) != 0)
    {
        std::cout << LMSG << "bind web_socket_port " << web_socket_port << " error" << std::endl;
        return -1;
    }
    if (socket_util::Listen(web_socket_fd) != 0)
    {
        std::cout << LMSG << "bind web_socket_port " << web_socket_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(web_socket_fd);
    socket_util::GetSocketName(web_socket_fd, local_ip, local_port);

    TcpSocket web_socket_socket(&epoller, web_socket_fd, std::bind(&ProtocolFactory::GenWebSocketProtocol, std::placeholders::_1, std::placeholders::_2));
    web_socket_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    web_socket_socket.EnableRead();
    web_socket_socket.AsServerSocket();

    // === Init SSL WebSocket Socket ===
    int ssl_web_socket_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(ssl_web_socket_fd);
    if (socket_util::Bind(ssl_web_socket_fd, "0.0.0.0", ssl_web_socket_port) != 0)
    {
        std::cout << LMSG << "bind ssl_web_socket_port " << ssl_web_socket_port << " error" << std::endl;
        return -1;
    }
    if (socket_util::Listen(ssl_web_socket_fd) != 0)
    {
        std::cout << LMSG << "bind ssl_web_socket_port " << ssl_web_socket_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(ssl_web_socket_fd);
    socket_util::GetSocketName(ssl_web_socket_fd, local_ip, local_port);

    SslSocket ssl_web_socket_socket(&epoller, ssl_web_socket_fd, std::bind(&ProtocolFactory::GenWebSocketProtocol, std::placeholders::_1, std::placeholders::_2));
    ssl_web_socket_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    ssl_web_socket_socket.EnableRead();
    ssl_web_socket_socket.AsServerSocket();

    // === Init Server Http File Socket ===
    int server_http_file_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(server_http_file_fd);
    if (socket_util::Bind(server_http_file_fd, "0.0.0.0", http_file_port) != 0)
    {
        std::cout << LMSG << "bind http_file_port " << http_file_port << " error" << std::endl;
        return -1;
    }

    if (socket_util::Listen(server_http_file_fd) != 0)
    {
        std::cout << LMSG << "bind http_file_port " << http_file_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(server_http_file_fd);
    socket_util::GetSocketName(server_http_file_fd, local_ip, local_port);

    TcpSocket server_http_file_socket(&epoller, server_http_file_fd, std::bind(&ProtocolFactory::GenHttpFileProtocol, std::placeholders::_1, std::placeholders::_2));
    server_http_file_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    server_http_file_socket.EnableRead();
    server_http_file_socket.AsServerSocket();

    // === Init Server Https File Socket ===
    int server_https_file_fd = socket_util::CreateNonBlockTcpSocket();

    socket_util::ReuseAddr(server_https_file_fd);
    if (socket_util::Bind(server_https_file_fd, "0.0.0.0", https_file_port) != 0)
    {
        std::cout << LMSG << "bind https_file_port " << https_file_port << " error" << std::endl;
        return -1;
    }
    if (socket_util::Listen(server_https_file_fd) != 0)
    {
        std::cout << LMSG << "bind https_file_port " << https_file_port << " error" << std::endl;
        return -1;
    }
    
    socket_util::SetNonBlock(server_https_file_fd);
    socket_util::GetSocketName(server_http_file_fd, local_ip, local_port);

    SslSocket server_https_file_socket(&epoller, server_https_file_fd, std::bind(&ProtocolFactory::GenHttpFileProtocol, std::placeholders::_1, std::placeholders::_2));
    server_https_file_socket.ModName(local_ip + ":" + Util::Num2Str(local_port));
    server_https_file_socket.EnableRead();
    server_https_file_socket.AsServerSocket();

    // === Init WebRTC Socket ===
    int webrtc_fd = socket_util::CreateNonBlockUdpSocket();

    socket_util::ReuseAddr(webrtc_fd);
    if (socket_util::Bind(webrtc_fd, "0.0.0.0", webrtc_port) != 0)
    {
        std::cout << LMSG << "bind webrtc_port " << webrtc_port << " error" << std::endl;
        return -1;
    }
    socket_util::SetNonBlock(webrtc_fd);

    UdpSocket server_webrtc_socket(&epoller, webrtc_fd, std::bind(&ProtocolFactory::GenWebrtcProtocol, std::placeholders::_1, std::placeholders::_2));
    server_webrtc_socket.ModName("udp");
    server_webrtc_socket.EnableRead();

    srt_startup();
    srt_setloglevel(srt_logging::LogLevel::note);

    SrtEpoller srt_epoller;
    srt_epoller.Create();

    int server_srt_fd = srt_socket_util::CreateSrtSocket();
    srt_socket_util::SetTransTypeLive(server_srt_fd);
    srt_socket_util::SetBlock(server_srt_fd, false);
    srt_socket_util::SetSendBufSize(server_srt_fd, 10*1024*1024);
    srt_socket_util::SetRecvBufSize(server_srt_fd, 10*1024*1024);
    srt_socket_util::SetUdpSendBufSize(server_srt_fd, 10*1024*1024);
    srt_socket_util::SetUdpRecvBufSize(server_srt_fd, 10*1024*1024);
    srt_socket_util::SetPeerIdleTimeout(server_srt_fd, 20*60*1000);
    srt_socket_util::SetLatency(server_srt_fd, 1000);
    if (srt_socket_util::Bind(server_srt_fd, "0.0.0.0", srt_port) != 0)
    {
        std::cout << LMSG << "bind srt_port " << srt_port << " error" << std::endl;
        return -1;
    }
    if (srt_socket_util::Listen(server_srt_fd) != 0)
    {
        std::cout << LMSG << "bind srt_port " << srt_port << " error" << std::endl;
        return -1;
    }

    SrtSocket server_srt_socket(&srt_epoller, server_srt_fd, std::bind(&ProtocolFactory::GenSrtProtocol, std::placeholders::_1, std::placeholders::_2));
    server_srt_socket.ModName("srt");
    server_srt_socket.EnableRead();
    server_srt_socket.AsServerSocket();

    // Event Loop
    while (true)
    {
        epoller.WaitIO(100);
        srt_epoller.WaitIO(0);
    }

    return 0;
}
