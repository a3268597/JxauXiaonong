#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""语音打断(barge-in)回归测试。

用法: cd Server && python test/interrupt_test.py
      (会加载 SenseVoice/VAD, 启动约 15~20 秒)

覆盖 4 件事:
  1. 只预开 TTS 连接、一个字都没提交时, 收尾不能报 "input text is valid"
  2. interrupt_all() 不能阻塞(它是在 asyncio 线程里被调用的)
  3. 打断后任务要立刻退出, 且 audio/ws 两个队列不能留残余音频
  4. 不打断时一切照常, 仍会发出 {"type":"tts","state":"end"}
"""
import os
import sys
import threading
import time

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BASE)
LOG_FILE = os.path.join(BASE, "logs", "assistant_%s.log" % time.strftime("%Y%m%d"))

from service_manager import ServiceManager  # noqa: E402

results = []


def log_offset():
    try:
        return os.path.getsize(LOG_FILE)
    except OSError:
        return 0


def new_log_since(pos):
    try:
        with open(LOG_FILE, "r", encoding="utf-8", errors="replace") as f:
            f.seek(pos)
            return f.read()
    except OSError:
        return ""


def check(name, ok, detail=""):
    results.append(ok)
    print("[%s] %s %s" % ("PASS" if ok else "FAIL", name, detail))


def main():
    sm = ServiceManager()

    # ---- 1. 空流收尾: 客户端听完唤醒音却发现没人说话, 会走 reset_services ----
    pos = log_offset()
    sm.tts_service.tts_set(on_data=sm._tts_on_data, on_complete=sm._tts_on_complete)
    time.sleep(0.5)
    sm.reset_services()
    time.sleep(1.5)
    tail = new_log_since(pos)
    bad = [ln for ln in tail.splitlines() if "input text is valid" in ln or "task-failed" in ln]
    check("空流收尾不报错", not bad, ("日志出现: " + bad[0][:70]) if bad else "")

    # ---- 2/3. 打断 ----
    pos = log_offset()
    sm.tts_service.tts_set(on_data=sm._tts_on_data, on_complete=sm._tts_on_complete)
    worker = threading.Thread(target=sm.chat_start_task,
                              args=("给我讲一个300字左右的完整小故事",), daemon=True)
    worker.start()
    time.sleep(2.5)
    t0 = time.time()
    sm.interrupt_all()
    cost = time.time() - t0
    check("interrupt_all 不阻塞", cost < 0.5, "耗时 %.2fs" % cost)
    worker.join(timeout=5)
    check("打断后任务及时退出", not worker.is_alive())
    time.sleep(2.5)
    left = sm.audio_queue.qsize() + sm.ws_send_queue.qsize()
    check("打断后无残余音频", left == 0, "残留 %d 条" % left)
    tail = new_log_since(pos)
    check("日志记录了打断与停止合成",
          "已打断当前播报" in tail and "停止后续合成" in tail)

    # ---- 4. 正常轮次仍要发 tts end ----
    sm._drain_queue(sm.ws_send_queue)
    sm.tts_service.tts_set(on_data=sm._tts_on_data, on_complete=sm._tts_on_complete)
    sm.chat_start_task("你好")
    time.sleep(2.0)
    ends = []
    while not sm.ws_send_queue.empty():
        item = sm.ws_send_queue.get_nowait()
        if isinstance(item, str) and '"tts"' in item:
            ends.append(item)
    check("正常轮次仍发 tts end", len(ends) == 1, str(ends[:1]))

    print("\n总计 %d/%d 通过" % (sum(1 for r in results if r), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
