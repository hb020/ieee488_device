#!/usr/bin/env python

# This is a test of the various devices, via a VXI-11.2 gateway.
# It tests both the devices, the gateway and the IVI backends to pyvisa-py
#
# Tests performed:
# 1. basic communication with the device
# 2. EOI/EOS handling
# 3. the ability to read and write large sets of data to and from the device
# 4. SRQ handling
#
# .. all via a choice of IVI backends: 
# * pyvisa-py (use at least 0.8.2 or the github versions from after 2026-07-30)
# * NI-Visa
# * Keyight visa
# * R&S visa
#
# TODO:
# ppoll, via DOCMD. Few gateways support that.
#
from vxi11_2_testsrq import VXI11_2_testssrq
import logging
import argparse
import sys

DEFAULT_GATEWAY_IP = "192.168.7.116"
DEFAULT_INST = 1
# TODO get this from the class
RESOURCE_MANAGERS = ['py', 'ni', 'keysight', 'rs']

# Configure logging, and set global log level (for pyvisa etc)
LOG_LEVEL = logging.INFO # DEBUG, INFO, WARNING, ERROR, CRITICAL

logging.basicConfig(
    level=LOG_LEVEL,
    format='%(asctime)s - %(levelname)s - %(name)s - %(message)s',
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger(__name__)
# Set log level for this module
    
if __name__ == "__main__":
    # Parse command line arguments    
    parser = argparse.ArgumentParser(description="Test SRQ handling for VXI-11.",
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("gateway_ip", type=str, nargs="?", default=DEFAULT_GATEWAY_IP, help="The IP address of the gateway device to use for tests.")
    parser.add_argument("-s", "--start-instrument", type=int, default=DEFAULT_INST, choices=range(0, 31), help="The starting instrument number on the bus to test.")    
    parser.add_argument("-e", "--end-instrument", type=int, default=DEFAULT_INST, choices=range(0, 31), help="The ending instrument number on the bus to test.")
    parser.add_argument("-V", "--visa-provider", type=str, default="", choices=RESOURCE_MANAGERS, help="The VISA provider to use. Default is the system default.")    
    parser.add_argument("-T", "--test", type=int, default=0, choices=range(0, 4), help="The test to run. 0 is all. 1 is individual SRQ tests. 2 is single emitter SRQ test. 3 is multiple emitter SRQ test.")    
    
    args = parser.parse_args()
    start_instrument = args.start_instrument
    end_instrument = args.end_instrument
    test_to_run = args.test
    if end_instrument < start_instrument:
        # swap them
        i = end_instrument
        end_instrument = start_instrument
        start_instrument = i
    gateway_ip = args.gateway_ip
    visa_provider = args.visa_provider
    
    try:
        t = VXI11_2_testssrq(visa_provider, gateway_ip, start_instrument, end_instrument)
    except Exception as e:
        logger.error(f"Failed to get resource manager for visa provider {visa_provider}: {e}")
        sys.exit(1) 
    
    ok = True
    logger.info(f"Starting SRQ tests for instruments {start_instrument} to {end_instrument} on gateway {gateway_ip}, using VISA provider \"{visa_provider}\"")
    if test_to_run == 0 or test_to_run == 1:
        if not t.test_individual_srqs(early_enable=True):
            ok = False
        if not t.test_individual_srqs(early_enable=False):
            ok = False
    if test_to_run == 0 or test_to_run == 2:
        if not t.test_one_emitting_srq():
            ok = False
    if test_to_run == 0 or test_to_run == 3:
        if not t.test_multiple_emitting_srq():
            ok = False
        
    logger.info("All tests completed: " + ("OK" if ok else "FAILED"))
    
