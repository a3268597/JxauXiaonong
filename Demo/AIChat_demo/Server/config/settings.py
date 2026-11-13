import os
import socket

import dashscope


def _prefer_ipv4(host, port, *args, **kwargs):
    """把 IPv4 地址排到解析结果最前面。

    阿里云的域名会同时返回 IPv6(AAAA) 记录, 但校园网/家宽通常没有 IPv6 路由。
    先撞 IPv6 的机器会一直干等到超时, 表现为 TTS 报
    "websocket connection could not be established within 5s", 或在 Windows 上
    直接抛 [WinError 10060] 连接尝试失败。
    这里只调整顺序、不丢弃任何结果, IPv6 正常的环境行为不变。
    """
    infos = _original_getaddrinfo(host, port, *args, **kwargs)
    return [info for info in infos if info[0] == socket.AF_INET] + list(infos)


_original_getaddrinfo = socket.getaddrinfo
socket.getaddrinfo = _prefer_ipv4


def _drop_outbound_proxy():
    """让服务端不依赖本机代理(Clash 之类)。

    桌面会话常把 http_proxy/https_proxy 注入终端环境; 一旦代理进程没在跑,
    requests/urllib3 与 websocket-client 会一直撞 127.0.0.1:<port>,
    表现为 LLM 的 ProxyError 和 TTS 连不上。阿里云公共端点与高德 API 都能直连,
    所以这里默认清掉。确实需要走代理时, 设 ECHO_KEEP_PROXY=1 保留原值。
    """
    if os.environ.get("ECHO_KEEP_PROXY"):
        return
    for var in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "WS_PROXY", "WSS_PROXY",
                "http_proxy", "https_proxy", "all_proxy", "ws_proxy", "wss_proxy"):
        os.environ.pop(var, None)


_drop_outbound_proxy()


class Settings:
    protocol_version = 2

    # ---- 密钥与端点: 一律"环境变量优先", 不要把个人 key 提交进仓库 ----
    #   DASHSCOPE_API_KEY            百炼控制台生成的 key(必填, 或由客户端 hello 下发)
    #   DASHSCOPE_BASE_HTTP_API_URL  仅在改用百炼专属实例时才需要设置
    #   INTENT_MODEL / CHAT_MODEL    覆盖默认模型名
    # 注: 这里去掉了原先写在明处的 key —— 实测它已经无法建立 TTS 连接,
    #     而且明文密钥留在公开仓库里本身就是隐患(历史里仍能找到, 请确认已吊销)。
    dashscope.api_key = os.environ.get("DASHSCOPE_API_KEY", "")

    if os.environ.get("DASHSCOPE_BASE_HTTP_API_URL"):
        dashscope.base_http_api_url = os.environ["DASHSCOPE_BASE_HTTP_API_URL"]

    # 模型选择(cosyvoice-v1 已下线, TTS 侧见 models/tts_model.py)
    INTENT_MODEL = os.environ.get("INTENT_MODEL", "qwen-turbo")   # 专门用于意图识别
    CHAT_MODEL = os.environ.get("CHAT_MODEL", "qwen-turbo")       # 用于常规对话

    # device
    ASR_DEVICE = "cpu"            # ASR 模型使用的设备
    # ASR_DEVICE = "cuda"            # ASR 模型使用的设备
    VAD_DEVICE = "cpu"            # ASR 模型使用的设备

    # 超时设置
    API_TIMEOUT = 10  # 秒

    def Set_API_Key(self, aliyun_api_key):
        # 客户端 hello 没带 key 时保留服务端已配置的值, 避免被空值覆盖
        if aliyun_api_key:
            dashscope.api_key = aliyun_api_key


if not dashscope.api_key:
    print("[settings] 提示: 未设置环境变量 DASHSCOPE_API_KEY, "
          "若客户端 hello 也没带 aliyun_api_key, LLM 与语音合成都会失败。")

global_settings = Settings()
