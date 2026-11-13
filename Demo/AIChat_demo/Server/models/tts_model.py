import dashscope
from dashscope.api_entities.dashscope_response import SpeechSynthesisResponse
from dashscope.audio.tts_v2 import *
import time
from tools.logger import logger


class TTSModel:

    def __init__(self):
        # 初始化 TTS 回调函数
        self.callback = None
        # 本次流里是否真的提交过文本(只预开连接时不能走 streaming_complete)
        self._has_text = False
        # 初始化 TTS 生成器
        self.synthesizer = SpeechSynthesizer(
            model="cosyvoice-v2",
            voice="longxiaochun_v2",
            format=AudioFormat.PCM_16000HZ_MONO_16BIT,
            callback=self.callback
        )

    class __tts_callback(ResultCallback):

        def __init__(self, on_open=None, on_complete=None, on_error=None, on_close=None, on_data=None):
            self.on_open = on_open or (lambda: logger.info("tts server-WS is open."))
            self.on_complete = on_complete or (lambda: logger.info("Speech synthesis task completed successfully."))
            self.on_error = on_error or (lambda message: logger.info(f"Speech synthesis task failed, {message}"))
            self.on_close = on_close or (lambda: logger.info("tts server-WS is closed."))
            self.on_data = on_data or (lambda data: logger.info(f"Audio result length: {len(data)}"))

        # 实现 ResultCallback 必需的方法
        def on_open(self):
            self.on_open()

        def on_complete(self):
            self.on_complete()

        def on_error(self, message: str):
            self.on_error(message)

        def on_close(self):
            self.on_close()

        def on_data(self, data: bytes) -> None:
            self.on_data(data)

    def tts_stream_set(self, on_open=None, on_complete=None, on_error=None, on_close=None, on_data=None):
        '''设置TTS回调函数, 提前打开ws连接'''
        self.callback = self.__tts_callback(on_open=on_open, on_complete=on_complete,
                                      on_error=on_error, on_close=on_close, on_data=on_data)
        self._has_text = False
        self.synthesizer=SpeechSynthesizer(
            model="cosyvoice-v2",
            voice="longxiaochun_v2",
            format=AudioFormat.PCM_16000HZ_MONO_16BIT,
            callback=self.callback
        )
        try:
            self.synthesizer.streaming_call('') # 先提前打开ws连接
            time.sleep(0.1)
        except Exception as e:
            logger.error(f"打开 TTS 连接失败: {str(e)}")
            return False

    def tts_stream_close(self):
        if not self._has_text:
            # 只预开了连接、一个字都没提交(例如 VAD 判定无人说话就直接回 idle):
            # 此时 streaming_complete 会让服务端报 InvalidParameter
            # "Please ensure input text is valid." 并返回 1007, 应该改用 cancel。
            self.tts_stream_cancel()
            return
        try:
            self.synthesizer.streaming_complete()
        except Exception as exc:
            # 连接没打开成功(例如网络抖动)时, 关闭会抛 InvalidTask,
            # 这里不应该打断 chat_start_task 的收尾
            logger.warning(f"TTS 关闭时忽略异常: {exc}")
            return
        logger.info(f"Request ID: {self.synthesizer.get_last_request_id()}")

    def tts_stream_cancel(self):
        """立即结束本次合成任务, 丢弃还没生成/还没送出去的音频"""
        self._has_text = False
        try:
            self.synthesizer.streaming_cancel(complete_timeout_millis=2000)
        except Exception as exc:
            logger.debug(f"取消 TTS 任务时忽略异常: {exc}")

    def tts_stream_speech_synthesis(self, text_chunk):
        '''流式语音合成

        :param: text_chunk: 文本片
        :return: 合成是否成功'''

        if text_chunk:
            try:
                self.synthesizer.streaming_call(text_chunk)
                self._has_text = True
            except Exception as e:
                logger.error(f"TTS 流式合成失败: {str(e)}")
                return False

