from typing import Optional
import sys
import os
import pyvisa
import pyvisa.constants
import logging

#region General setup
#resource_managers: the below plus default
# On MacOS, only pyvisa-py and NI-VISA are supported, but the latter only as "default"
# and so far, I haven't found a way to see what is the default.
RESOURCE_MANAGERS = ['py', 'ni', 'keysight', 'rs']

# Choose one of the 2
# EVENT_MECH = pyvisa.constants.EventMechanism.queue
EVENT_MECH = pyvisa.constants.EventMechanism.handler

SRQ_WAIT_TIME = 2.0 # seconds to wait for SRQ to be received, after emitting it

#endregion
#region Logging setup
# Configure logging, and set global log level (for pyvisa etc)
LOG_LEVEL = logging.INFO # DEBUG, INFO, WARNING, ERROR, CRITICAL

logging.basicConfig(
    level=LOG_LEVEL,
    format='%(asctime)s - %(levelname)s - %(name)s - %(message)s',
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger(__name__)
# Set log level for this module
logger.setLevel(LOG_LEVEL)
#endregion  


#region test base class
class VXI11_2_Base:
    """
    Base class for VXI-11 tests.
    """
    
    def __init__(self, visa_provider: Optional[str], gateway_ip: str, inst_addresses: str):
        """Initialize the VXI-11.2 base test case.

        :param visa_provider: The VISA provider to use ('py', 'ni', 'keysight', 'rs', or None).
                          None, or any string other than the recognized providers,  ('py', 'ni', 'keysight', 'rs') 
                          all designate the system default provider.
        :type visa_provider: str, optional
        :param gateway_ip: The IP address of the VXI-11.2 gateway
        :type gateway_ip: str, optional
        :param inst_addresses: A string specifying the instrument addresses, separated by ';'. 
                               Addresses may contain secondary addresses, in which case the format is "{primary},{secondary}"}.
                               Examples: "1" or "1;2,0;2,1"
        :type inst_addresses: str
        :raises Exception: If the specified provider is not available on the current platform.
        :raises ValueError: If the instrument addresses are invalid.
        """
        self.gateway_ip = gateway_ip
        self.inst_addresses = self.extract_instrument_addresses(inst_addresses)
        if self.inst_addresses is None:
            raise ValueError(f"Invalid instrument addresses: {inst_addresses}")
        self.visa_provider = visa_provider
        self._inst_contexts = {}
        self.rm, self.visa_provider = self.get_resource_manager(visa_provider)
        logger.info(f"Using VISA provider: {self.visa_provider}")
        self.prepare_instrument_context()
        
    @classmethod
    def extract_instrument_addresses(cls,inst_addresses: str) -> list[str] | None:
        """Validate the format of the instrument addresses string.

        :param inst_addresses: A string specifying the instrument addresses, separated by ';'. 
                               Addresses may contain secondary addresses, in which case the format is "{primary},{secondary}"}.
                               Examples: "1" or "1;2,0;2,1"
        :type inst_addresses: str
        :return: The list of addresses if the format is valid, None otherwise.
        :rtype: list[str] | None
        """
        if not inst_addresses:
            return None
        addresses = inst_addresses.split(';')
        if (len(addresses) == 0):
            logger.error("No instrument addresses specified")
            return None
        for addr in addresses:
            parts = addr.split(',')
            if len(parts) == 0 or len(parts) > 2:
                logger.error(f"Invalid instrument address format: {addr}")
                return None
            try:
                primary = int(parts[0])
                if primary < 0 or primary > 30:
                    logger.error(f"Invalid primary address: {primary}")
                    return None
                if len(parts) == 2:
                    secondary = int(parts[1])
                    if secondary < 0 or secondary > 30:
                        logger.error(f"Invalid secondary address: {secondary}")
                        return None
            except ValueError:
                logger.error(f"Invalid instrument address format: {addr}")
                return None
        return addresses
    
    def close(self):
        """Close all opened instruments and the resource manager."""
        for inst_nr, _ in self._inst_contexts.items():
            self.close_instrument(inst_nr)
        if hasattr(self, 'rm') and self.rm is not None:
            self.rm.close()
            self.rm = None
            
    def __del__(self):
        """Destructor to clean up resources."""
        self.close()
        
    @classmethod
    def get_possible_visa_providers(cls) -> list[str]:
        """Get a list of possible VISA providers for the current platform.

        :return: A list of possible VISA providers.
        :rtype: list[str]
        """
        return RESOURCE_MANAGERS
    
    def get_resource_manager(self, visa_provider: Optional[str]) -> tuple[pyvisa.ResourceManager, str]:
        """
        Get a PyVISA ResourceManager for the specified VISA provider.
        
        :param visa_provider: The VISA provider name ('py', 'ni', 'keysight', 'rs', or None).
                            None, or any string other than the recognized providers,  ('py', 'ni', 'keysight', 'rs') 
                            all designate the system default provider.
        
        :returns: A tuple of (ResourceManager, provider_name) where provider_name is the resolved provider.
        :raises Exception: If the specified provider is not available on the current platform.
        """
        if visa_provider is None:
            visa_provider = ''
        if not isinstance(visa_provider, str):
            visa_provider = str(visa_provider)
        visa_provider = visa_provider.strip().lower()
            
        if visa_provider == RESOURCE_MANAGERS[0]:
            return pyvisa.ResourceManager('@py'), visa_provider
        if visa_provider == RESOURCE_MANAGERS[1]:
            if sys.platform == 'win32' or sys.platform == 'win64':
                libpath = 'C:/Windows/System32/nivisa64.dll'
                if os.path.exists(libpath):
                    return pyvisa.ResourceManager(libpath), visa_provider
                else:
                    raise Exception(f"NI VISA cannot be found: no '{libpath}' on this system")
            else:
                raise Exception(f"NI VISA cannot be selected (yet) on {sys.platform}")
        if visa_provider == RESOURCE_MANAGERS[2]:
            if sys.platform == 'win32' or sys.platform == 'win64':
                libpath = 'C:/Program Files (x86)/IVI Foundation/VISA/WinNT/ktvisa/ktbin/visa32.dll'
                if os.path.exists(libpath):
                    os.add_dll_directory("C:/Program Files/Keysight/IO Libraries Suite/bin")
                    os.add_dll_directory("C:/Program Files (x86)/Keysight/IO Libraries Suite/bin")
                    return pyvisa.ResourceManager(libpath), visa_provider
                else:
                    raise Exception(f"Keysight VISA cannot be found: no '{libpath}' on this system")
            else:
                raise Exception(f"Keysight VISA cannot be selected (yet) on {sys.platform}")
        if visa_provider == RESOURCE_MANAGERS[3]:
            if sys.platform == 'win32' or sys.platform == 'win64':
                libpath = 'C:/Program Files (x86)/IVI Foundation/VISA/WinNT/RsVisa/bin/visa32.dll'
                if os.path.exists(libpath):
                    return pyvisa.ResourceManager(libpath), visa_provider
                else:
                    raise Exception(f"R&S VISA cannot be found: no '{libpath}' on this system")
            else:
                raise Exception(f"R&S VISA cannot be selected (yet) on {sys.platform}")
        else:
            return pyvisa.ResourceManager(''), 'default' # TODO: find out what visa this really is

    
    def prepare_instrument_context(self) -> None:
        """Initialize the instrument contexts for the specified range of instruments.
        """
        inst_nr = 0
        if not hasattr(self, 'inst_addresses') or self.inst_addresses is None:
            logger.error("No instrument addresses specified")
            return
        for addr in self.inst_addresses:
            inst_nr += 1  # list is 1 based, not 0 based
            self._inst_contexts[inst_nr] = {}
            self._inst_contexts[inst_nr]["called"] = 0
            self._inst_contexts[inst_nr]["opened"] = False
            self._inst_contexts[inst_nr]["inst"] = None
            self._inst_contexts[inst_nr]["address"] = addr
            self._inst_contexts[inst_nr]["resource_name"] = self.get_resourcename_for_instrument(addr)
            self._inst_contexts[inst_nr]["cmds_init"] = []
            
    def get_resourcename_for_instrument(self, addr: str) -> str:
        """Get the VXI-11.2 VISA compatible resource name for the given instrument bus address.

        :param addr: The instrument bus address (may have secondary address, in the format "{primary},{secondary}").
        :type addr: str
        :return: The VXI-11.2 VISA compatible resource name
        :rtype: str
        """
        if self.visa_provider == "rs":
            return f"TCPIP::{self.gateway_ip}::inst{addr}::INSTR"
        else:
            return f"TCPIP::{self.gateway_ip}::gpib0,{addr}::INSTR"
        
    def open_instrument(self, inst_nr: int) -> bool:
        if not hasattr(self, 'rm') or self.rm is None:
            logger.error("Resource manager is not initialized")
            return False
        
        resource_name = self._inst_contexts[inst_nr]["resource_name"]
        try:
            inst = self.rm.open_resource(resource_name)
        except Exception as e:
            logger.error(f"Failed to open {resource_name}: {e}")
            return False
        inst.timeout = 1000
        if inst is None or not (
                isinstance(inst, pyvisa.resources.TCPIPInstrument) or 
                isinstance(inst, pyvisa.resources.GPIBInstrument) or
                isinstance(inst, pyvisa.resources.USBInstrument)):
            logger.error(f"Failed to open {resource_name}")
            return False
        
        self._inst_contexts[inst_nr]["opened"] = True
        self._inst_contexts[inst_nr]["inst"] = inst
        try:
            idn = inst.query("*IDN?").strip()
            if idn == "":
                idn = inst.query("*ID?").strip()
        except pyvisa.VisaIOError as e:
            logger.error(f"Failed to query IDN for {resource_name}: {e}")
            inst.close()
            self._inst_contexts[inst_nr]["opened"] = False
            self._inst_contexts[inst_nr]["inst"] = None
            return False
        # logger.debug(f"IDN: {idn}")
        
        context = self.make_instrument_context(inst_nr, idn)

        if "cmds_init" not in context or len(context["cmds_init"]) == 0:
            logger.error(f"No initialization commands for {idn}, cannot run the tests")
            inst.close()
            self._inst_contexts[inst_nr]["opened"] = False
            self._inst_contexts[inst_nr]["inst"] = None
            return False

        for k,v in context.items():
            self._inst_contexts[inst_nr][k] = v
        return True
    
    def make_instrument_context(self, inst_nr: int, idn: str) -> dict:
        """Create and initialize the instrument context for the given instrument number.
        
        It must return at least a dict with the following keys:
        - "cmds_init": a list of commands to initialize the instrument for testing. This is not allowed to be empty.

        :param inst_nr: The instrument number
        :type inst_nr: int
        :param idn: The identification string of the instrument
        :type idn: str
        :return: a dict with all test specific information for the instrument
        :rtype: dict
        """
        return { "cmds_init": ["*CLS"]}
    
    def run(self) -> bool:
        """Run the tests for all instruments in the specified range.

        :return: True if all tests passed, False otherwise
        :rtype: bool
        """
        all_passed = True
        for inst_nr, context in self._inst_contexts.items():
            resource_name = context["resource_name"]
            logger.info(f"Setting up instrument {inst_nr} at {resource_name}")
            if not self.open_instrument(inst_nr):
                logger.error(f"Failed to setup instrument {inst_nr} at {resource_name}")
                all_passed = False
                continue
            
            inst = context["inst"]
            cmds_init = context["cmds_init"]
            logger.info(f"Initializing instrument {inst_nr} with commands: {cmds_init}")
            for cmd in cmds_init:
                try:
                    inst.write(cmd)
                except pyvisa.VisaIOError as e:
                    logger.error(f"Failed to write command '{cmd}' to instrument {inst_nr}: {e}")
                    all_passed = False
                    break
            
            if not self.test_instrument(inst_nr, context):
                all_passed = False
        
        return all_passed
    
    
    def test_instrument(self, inst_nr: int, context: dict) -> bool:
        """Run the actual tests for the given instrument number.
        
        This method should be overridden in a subclass to implement specific tests.
        
        The context parameter contains all test specific information for the instrument, 
        as created by the make_instrument_context method and populated in the setup_instrument method. 
        
        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context for the given instrument. This context is created by the make_instrument_context method and contains all test specific information for the instrument.
        :type context: dict
        :return: True if all tests passed, False otherwise
        :rtype: bool
        """
        logger.info(f"Running tests for instrument {inst_nr}")
        # Implement specific tests here
        return True

    def close_instrument(self, inst_nr: int) -> bool:
        inst = self._inst_contexts[inst_nr]["inst"]
        if inst is not None:
            if self._inst_contexts[inst_nr]["opened"]:
                inst.close()
                self._inst_contexts[inst_nr]["opened"] = False
                self._inst_contexts[inst_nr]["inst"] = None
        return True

#endregion