# helper functions for the various tests

def ieee488_device_longrd_query(length: int) -> tuple[str, str]:
    """ Return the long read query and expected reply

    :param length: length of the expected reply
    :type length: int
    :raises ValueError: if the data could not be created
    :return: a tuple containing the long read query and the expected reply
    :rtype: tuple[str, str]  query, expected reply
    """
    data = "".join(chr(0x30 + (i % (0x7E - 0x30 + 1))) for i in range(length))
    if len(data) != length:
        raise ValueError(f"Failed to create data of length {length}, got length {len(data)}")
    return f"LONGRD? {length}", data

def ieee488_device_longwr_query(length: int) -> tuple[str, str]:
    """ Return the long write query and expected reply

    :param length: length of the expected reply
    :type length: int
    :raises ValueError: if the data could not be created
    :return: a tuple containing the long write query and the expected reply
    :rtype: tuple[str, str]  query, expected reply
    """
    data = "".join(chr(0x30 + (i % (0x7E - 0x30 + 1))) for i in range(length))
    if len(data) != length:
        raise ValueError(f"Failed to create data of length {length}, got length {len(data)}")
    return f"LONGWR? {data}", f"{length},48,\"\""

def str_diff(str1: str, str2: str) -> str:
    """Return a string showing the differences between two strings, if any.

    :param str1: The first string
    :type str1: str
    :param str2: The second string
    :type str2: str
    :return: A string showing the differences between the two strings, or an empty string if they are the same. This string has NO newlines embedded, so it can be used in a single-line log message.
    :rtype: str
    """
    diff = []
    if str1 == str2:
        return ""
    for i, (c1, c2) in enumerate(zip(str1, str2)):
        if c1 != c2:
            diff.append(f"Pos {i}: '{c1}' != '{c2}'")
    if len(str1) != len(str2):
        diff.append(f"Length mismatch: {len(str1)} != {len(str2)}")
    return " | ".join(diff)