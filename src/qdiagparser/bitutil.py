def bits(value: int, start: int, length: int) -> int:
    return (value >> start) & ((1 << length) - 1)


def bitstream_lsb_words(words: list[int]) -> int:
    value = 0
    for word in reversed(words):
        value = (value << 32) | (word & 0xFFFFFFFF)
    return value

