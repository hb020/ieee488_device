#!/usr/bin/env python

# This is a test of the various devices, via a VXI-11.2 gateway or any other VISA device.
# It tests both the devices, the gateway/device and the IVI backends to pyvisa-py
#
# Tests performed:
# 1. basic communication with the device
# 2. EOI/EOS handling
# 3. the ability to read and write large sets of data to and from the device
# 4. SRQ handling
# .....
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
from visadevice_base import visadevice_base
from visadevice_testsrq import visadevice_testsrq
from visadevice_longrd import visadevice_longrd
from visadevice_longwr import visadevice_longwr
from visadevice_end import visadevice_end
from visadevice_delay import visadevice_delay
import logging
import argparse
import sys

DEFAULT_IP = "192.168.7.116"
DEFAULT_INST = 1
DEFAULT_PROVIDER = ""
DEFAULT_TEST = 0
DEFAULT_TYPE = "gateway"  # "vxi11", "hislip", "socket", "gateway"
# DEFAULT_IP = "127.0.0.1"
# DEFAULT_INST = 0
# DEFAULT_PROVIDER = "py"
# DEFAULT_TEST = 8

# Configure logging, and set global log level (for pyvisa etc)
LOG_LEVEL = logging.INFO # DEBUG, INFO, WARNING, ERROR, CRITICAL

logging.basicConfig(
    level=LOG_LEVEL,
    format='%(asctime)s - %(levelname)s - %(name)s - %(message)s',
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger(__name__)

    
if __name__ == "__main__":
    # Parse command line arguments
    
    # The names to use for the tests, indexed by test number. The first test is 0, which means all tests.
    # Use 2 digits for the number, so that the list is nicely aligned. The first test is 0, which means all tests.
    test_names = " 0 All\n"
    # The test classes to use
    testers = [visadevice_base, visadevice_end, visadevice_testsrq, visadevice_longrd, visadevice_longwr, visadevice_delay]
    # the file names. Not much logging in their name, but anyway.
    logger_names = ["visadevice_base", "visadevice_end", "visadevice_testsrq", "visadevice_longrd", "visadevice_longwr", "visadevice_delay"]
    # the array of testers, indexed by test number. The first tester is the base tests, the second is the SRQ tests.
    test_steps = []
    global_test_nr = 1
    for tester in testers:
        tester_steps = tester.testmethods()
        local_test_nr = 0
        for step in tester_steps:
            test_steps.append((tester, step, global_test_nr, local_test_nr))
            local_test_nr += 1
            test_names = test_names + f"{global_test_nr:2} {step}\n"
            global_test_nr += 1

    parser = argparse.ArgumentParser(description="Test VXI-11.2 gateways and other VISA devices",
                                     formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("device_ip", type=str, nargs="?", default=DEFAULT_IP, help="The IP address of the device to use for tests.")
    parser.add_argument("-t", "--type", type=str, default=DEFAULT_TYPE, choices=["vxi11", "hislip", "socket", "gateway"], help=f"The type of device to test. Default is {DEFAULT_TYPE}.")
    parser.add_argument("-p", "--port", type=int, default=0, help="The port to use for the device. Default is 0, which means the default port for the device type.\n This is only used for socket and hislip types, and their default ports are respectively 5025 and 4880.")
    parser.add_argument("-a", "--addresses", type=str, default=str(DEFAULT_INST), help="The addresses on the bus, separated by ';'.\nAddresses may contain secondary addresses, in which case the format is '{primary},{secondary}'.\nExamples: '1' or '1;2,0;2,1'.\nIs ignored for socket type.")
    parser.add_argument("-V", "--visa-provider", type=str, default=DEFAULT_PROVIDER, choices=visadevice_base.get_possible_visa_providers(), help="The VISA provider to use. Default is the system default.")
    parser.add_argument("-T", "--test", type=int, default=DEFAULT_TEST, choices=range(0, len(test_steps)+1), help=test_names)
    parser.add_argument("-cs", "--auto-chunk-size", action="store_true", help="Enable automatic chunk size correction, needed with some gateways for the long reads/writes.")
    parser.add_argument("-L", "--log-level", type=str.upper, default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"], help="The logging level.")
    parser.add_argument("--discover", type=str, nargs="?", const=None, default=False, choices=["id"], metavar="id", help="Discover the VISA devices auto discoverable by this system, and exit.\nIf provided, this uses the log level setting and the visa provider setting when discovering.\nThe result will also print the output from '*IDN?' if you specify 'id'. This may however disturb older devices.")
    
    options = {}
    args = parser.parse_args()
    # set log levels
    log_level = getattr(logging, args.log_level.upper(), logging.INFO)
    # set the log level of all loggers used in this program
    # set my log level
    logger.setLevel(log_level)
    # The test classes gave their own loggers.
    for tester in testers:
        logging.getLogger(f"{tester.__name__}").setLevel(log_level)
        
    logging.getLogger("pyvisa").setLevel(log_level)
    try:
        logging.getLogger("pyvisa-py").setLevel(log_level)
    except Exception as e:
        pass
    # The files themselves also have loggers for various stuff.
    for logger_name in logger_names:
        logging.getLogger(logger_name).setLevel(log_level)
        
    visa_provider = args.visa_provider
    if len(visa_provider) == 0:
        visa_provider = None
        
    if args.discover is not False:
        logger.info(f"Discovering VISA devices using provider {visa_provider if visa_provider else 'default'}...")
        try:
            # the visadevice_base class is used to discover the devices, 
            # because it has the discover_visa_devices() method. The Ip address etc are just dummy values
            t = visadevice_base(visa_provider, "127.0.0.1", "0", "gateway", 0, {})
        except Exception as e:
            logger.error(f"Failed to get resource manager for visa provider {visa_provider}: {e}")
            sys.exit(1) 
        
        devices = t.discover_visa_devices(args.discover == "id")
        if len(devices) == 0:
            logger.info("No VISA devices found.")
        else:
            print(f"Found {len(devices)} VISA devices:")
            for device in devices:
                print(f"  {device}")
        sys.exit(0)
        
    options["auto_chunk_size"] = args.auto_chunk_size
        
    addresses = args.addresses
    
    test_to_run = args.test
    device_ip = args.device_ip
    visa_type = args.type
    port = args.port    
    if visa_type == "socket":
        addresses = "0"  # socket type does not use addresses, but we need to pass something to the test classes.
    
    if isinstance(addresses, int):
        addresses = str(addresses)
    address_list = visadevice_base.validate_instrument_addresses(addresses, visa_type)
    if address_list is None:
        logger.error(f"Invalid instrument addresses: {addresses}")
        sys.exit(1)
        
    base_connstring = visadevice_base.get_resourcename_for_instrument(device_ip, visa_provider if visa_provider else 'default', "{addr}", visa_type, port)
    logger.info(f"Using connection: '{base_connstring}', addresses: '{addresses}', VISA provider: '{visa_provider if visa_provider else 'default'}', test to run: {test_to_run if test_to_run != 0 else 'all'}, auto chunk size: {options['auto_chunk_size']}, log level: {args.log_level}")
    
    # determine tests to run
    ok = True
    skipped = 0
    failed = 0
    succeeded = 0
    for i, (tester, step, global_test_nr, local_test_nr) in enumerate(test_steps):
        if global_test_nr == test_to_run or test_to_run == 0:
            try:
                t = tester(visa_provider, device_ip, addresses, visa_type, port, options)
            except Exception as e:
                logger.error(f"Failed to get resource manager for visa provider {visa_provider}: {e}")
                sys.exit(1) 
            
            t.skipped = 0
            if not t.run(local_test_nr):
                ok = False
            skipped += t.skipped
            succeeded += t.succeeded
            failed += t.failed
            t.close()

    logger.info(f"All tests completed: {'OK' if ok else 'FAILED'}, {skipped} tests skipped, {succeeded} tests succeeded, {failed} tests failed.")
    
