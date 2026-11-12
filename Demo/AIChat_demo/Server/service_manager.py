from services.vad_service import VADService
from services.asr_service import ASRService
from services.chat_service import ChatService
from services.tts_service import TTSService
from tools.registry import global_registry
from services.intent_service import IntentService
from tools.audio_processor import AudioProcessor
from threads.task_manager import TaskManager
from tools.logger import logger
import queue
import threading
import json

class ServiceManager:
    def __init__(self):
        # 初始化服务
        self.audio_processor = AudioProcessor()
        self.vad_service = VADService()
        self.asr_service = ASRService()
        self.intent_service = IntentService(global_registry)
        self.chat_service = ChatService()
        self.tts_service = TTSService()
        self.is_vad = False  # 防止VAD发生后还语音加入

        self.tts_text_queue = queue.Queue() # 用于存放 TTS 生成的文本
        self.audio_queue = queue.Queue()    # 用于存放生成的音频数据
        self.ws_send_queue = queue.Queue()  # 用于存储ws需要发送的数据

        self.stop_event = threading.Event() # 用于控制线程停止
        self.interrupt_event = threading.Event()  # 用户在播报中说了唤醒词, 需要中止当前回复

        self.task_manager = TaskManager()   # 短生命周期的任务管理器

        def continue_chat():
            return "继续聊天..."

        def handle_exit_intent():
            return "再见！"

        # 默认的一些意图注册到系统
        global_registry.register_function("continue_chat", "继续聊天意图", {}, continue_chat)
        global_registry.register_function("exit_chat", "结束对话意图", {}, handle_exit_intent)

    def _drain_queue(self, q):
        """丢弃队列里积压的数据"""
        while True:
            try:
                q.get_nowait()
            except queue.Empty:
                return

    def interrupt_all(self):
        """用户打断: 停掉正在生成的回复, 并丢弃还没发出去的音频

        客户端在播报期间再次检测到唤醒词时发 {"type":"interrupt"} 过来,
        这里要让 chat_start_task 尽快退出, 并把 audio/ws 两个队列清空,
        否则机器人会先把剩下的半句话播完才闭嘴。
        """
        # 先置位再清空, 保证清空之后到达的残余音频也会被 _tts_on_data 丢掉
        self.interrupt_event.set()
        self._drain_queue(self.audio_queue)
        self._drain_queue(self.ws_send_queue)

        def _close_tts():
            try:
                # 用 cancel 而不是 close: 已提交但没播的文本会被直接丢弃, 不再合成
                self.tts_service.tts_cancel()
            except Exception as exc:
                logger.debug(f"打断时取消 TTS 忽略异常: {exc}")

        # streaming_complete 会阻塞等待已提交文本合成完毕, 放到后台线程执行,
        # 否则会卡住 WebSocket 的 asyncio 事件循环
        threading.Thread(target=_close_tts, daemon=True).start()
        self.is_vad = False
        self.vad_service.reset()
        self.asr_service.reset()
        logger.info("已打断当前播报, 等待新的指令")

    def reset_services(self):
        """
        重置所有服务的状态
        """
        self.is_vad = False
        self.vad_service.reset()
        self.asr_service.reset()
        self.chat_service.chat_clear()
        try:
            self.tts_service.tts_close()
        except Exception as e:
            pass

    def _tts_on_data(self, data):
        """
        TTS 生成回调函数
        :param data: 生成的音频数据
        """
        # 已打断: 合成器里排队的句子还会继续回调, 这里直接丢弃, 不再入队
        if self.interrupt_event.is_set():
            return
        # 将生成的音频数据放入语音队列
        self.audio_queue.put(data)
        # logger.info(f"Received TTS data: {len(data)} bytes")

    def _tts_on_complete(self):
        # 打断时后台的 tts_close 也会回调到这里, 这轮已经作废, 不要再发结束信号
        if self.interrupt_event.is_set():
            return
        msg = {
            "type": "tts",
            "state": "end",
        }
        self.ws_send_queue.put(json.dumps(msg))

    def chat_start_task(self, text):
        """
        处理识别到的文本，进行对话
        :param self: ServiceManager 实例
        :param text: 文本
        """
        self.interrupt_event.clear()
        # 1.进行意图识别
        function_calls = self.intent_service.detect_intent(text)
        history_list = []
        # 2.执行函数调用（如果有）
        for function_call in function_calls:
            if "function_call" in function_call and "name" in function_call["function_call"]:
                logger.info(f"[准备调用] {function_call}")
                # 执行函数调用
                if function_call["function_call"]["name"] == "continue_chat":
                    # 继续聊天意图
                    pass
                elif function_call["function_call"]["name"] == "exit_chat":
                    # 结束对话意图
                    response =  {
                            "type": "chat",
                            "dialogue": "end"
                    }
                    self.ws_send_queue.put(json.dumps(response))
                else:
                    # 其他函数调用, 发送到Client端, Client自己处理
                    self.ws_send_queue.put(json.dumps(function_call))
                    history_list.append([
                        {"role": "user", "content": f"函数调用: {function_call}"},
                        {"role": "assistant", "content": f"函数调用完成"}
                    ])
        # 3.调用聊天服务生成文字
        if self.interrupt_event.is_set():
            logger.info("意图识别阶段已被打断, 放弃本轮回复")
            return
        answers = self.chat_service.generate_chat_response(text, history=history_list, is_stream=True)
        if answers == -1:
            logger.error("LLM 生成失败")
            return -1
        logger.info(f"[回复]: ")
        # 4.直接TTS生成
        interrupted = False
        for text_chunk in answers:
            if self.interrupt_event.is_set():
                interrupted = True
                logger.info("生成过程中被用户打断, 停止后续合成")
                break
            print(text_chunk, end="", flush=True)
            # 调用 TTS 服务进行语音合成
            self.tts_service.tts_speech_stream(text_chunk)
        print()  # 换行
        if not interrupted:
            # 关闭 TTS 流
            self.tts_service.tts_close()
