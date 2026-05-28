# utils.py

import time


class RingBuffer:
    """Кольцевой буфер для хранения последних N элементов"""

    def __init__(self, max_size=100):
        self.max_size = max_size
        self._cur_ind = 0
        self._list = [None] * max_size

    def add(self, elem):
        self._list[self._cur_ind] = elem
        self._cur_ind = (self._cur_ind + 1) % self.max_size

    def __contains__(self, elem):
        return elem in self._list

    def clear(self):
        self._list = [None] * self.max_size
        self._cur_ind = 0


class LoRaTxQueue:
    """
    Ограниченная (по количеству) очередь сообщений LoRa (bytes).
    Реализация кольцевым буфером: без роста списков, предсказуемая RAM.

    add(msg_bytes) -> True/False (False, если переполнена/неверные данные)
    pop() -> bytes|None
    """

    def __init__(self, max_items=24, max_msg_len=240):
        self.max_items = int(max_items)
        self.max_msg_len = int(max_msg_len)

        self._buf = [None] * self.max_items  # bytes
        self._head = 0
        self._tail = 0
        self._size = 0

        # Тайминг "не чаще чем раз в N мс"
        self._next_send_ms = time.ticks_ms()

    def __len__(self):
        return self._size

    def empty(self) -> bool:
        return self._size == 0

    def full(self) -> bool:
        return self._size >= self.max_items

    def clear(self):
        self._buf = [None] * self.max_items
        self._head = 0
        self._tail = 0
        self._size = 0

    def add(self, msg) -> bool:
        """
        msg: bytes|bytearray|memoryview
        """
        if msg is None:
            return False

        if not isinstance(msg, (bytes, bytearray, memoryview)):
            return False

        # приводим к bytes (упрощает дальнейшую работу)
        if not isinstance(msg, bytes):
            msg = bytes(msg)

        if len(msg) > self.max_msg_len:
            return False

        if self.full():
            return False

        self._buf[self._tail] = msg
        self._tail = (self._tail + 1) % self.max_items
        self._size += 1
        return True

    def pop(self):
        if self.empty():
            return None
        msg = self._buf[self._head]
        self._buf[self._head] = None
        self._head = (self._head + 1) % self.max_items
        self._size -= 1
        return msg

    # --- pacing helpers ---
    def can_send_now(self) -> bool:
        return time.ticks_diff(time.ticks_ms(), self._next_send_ms) >= 0

    def mark_sent(self, min_interval_ms: int):
        self._next_send_ms = time.ticks_add(time.ticks_ms(), int(min_interval_ms))


def lora_tx_pump(lora, tx_queue: LoRaTxQueue, min_interval_ms: int = 1200) -> str:
    """
    "Прокачка" TX очереди: отправляет максимум ОДНО сообщение за вызов,
    выдерживая паузу min_interval_ms между отправками.

    Возвращает:
      - "empty" если очередь пуста
      - "wait"  если ещё рано отправлять (интервал не прошёл)
      - "sent"  если отправили
      - "err"   если send_bytes() упал

    Важно: sleep здесь нет — вызывайте функцию из main loop часто.
    """
    if tx_queue.empty():
        return "empty", None

    if not tx_queue.can_send_now():
        return "wait", None

    msg = tx_queue.pop()
    if msg is None:
        return "empty", None

    try:
        lora.send_bytes(msg)
        tx_queue.mark_sent(min_interval_ms)
        return "sent", msg
    except Exception as e:
        print("LoRa TX error:", e)
        return "err", None


def lora_tx_pump_new(lora, tx_queue: LoRaTxQueue, min_interval_ms: int = 1200) -> tuple:
    """
    Отправляет максимум 1 сообщение за вызов.
    Возвращает (status, msg_or_none):
      - ("empty", None)
      - ("wait", None)
      - ("sent", msg_bytes)
      - ("busy", msg_bytes)   # модуль не готов / AUX timeout
      - ("err", msg_bytes)    # ошибка send
    """
    if tx_queue.empty():
        return ("empty", None)

    if not tx_queue.can_send_now():
        return ("wait", None)

    msg = tx_queue.peek()
    if msg is None:
        return ("empty", None)

    # Если у LoRaMiniLib есть WOR — переключаемся в normal на время TX
    try:
        if hasattr(lora, "set_mode_normal"):
            lora.set_mode_normal()
    except Exception as e:
        print("set_mode_normal error:", e)

    rc = lora.send_bytes(msg)  # 0=ok, 1=busy/timeout, -1=uart mismatch
    if rc == 0:
        tx_queue.drop()
        tx_queue.mark_sent(min_interval_ms)

        # после TX можно вернуть WOR (если поддерживаете)
        try:
            if hasattr(lora, "set_mode_wor_rx"):
                lora.set_mode_wor_rx()
        except Exception as e:
            print("set_mode_wor_rx error:", e)

        return ("sent", msg)

    # не удаляем msg из очереди — попробуем позже
    tx_queue.mark_sent(min_interval_ms)

    if rc == 1:
        return ("busy", msg)
    return ("err", msg)