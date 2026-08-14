import pyvisa

def testcase(provider, size: int):
    if provider == '@py':
        print(f"******* provider = 'pyvisa-py', data size = {size} *******")
    else:
        print(f"******* provider = 'NI-VISA', data size = {size} *******")
    rm = pyvisa.ResourceManager(provider)
    inst = rm.open_resource('TCPIP::192.168.7.116::inst5::INSTR')
    inst.timeout = 3000
    data_size = size - 2
    myquery = f"longrd? {data_size}"  # this is query to a custom device that can return any length of data, up to 2^32-1 bytes

    # Measure the exact response length (large chunk_size rules out the bug)
    inst.chunk_size = 10 * 1024 * 1024
    inst.write(myquery)
    L = len(inst.read_raw())
    
    for chunk_size in range(L-9, L+11):
        # do a series of number of bytes less and more
        inst.chunk_size = chunk_size
        inst.write(myquery)
        try:
            inst.read_raw()          # -> fails, reproducibly
        except Exception as e:
            print(f'read_raw() with chunk_size {inst.chunk_size} failed: {e}')
            continue
        print(f'read_raw() with chunk_size {inst.chunk_size} succeeded')
    
    
if __name__ == '__main__':
    testcase('@py', 10)
    testcase('@py', 2000)
    testcase('', 10)
    testcase('', 2000)
