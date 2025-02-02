/********************************************************************
 * This file includes functions that output a fabric-dependent 
 * bitstream database to files in plain text
 *******************************************************************/
#include <chrono>
#include <ctime>
#include <fstream>

/* Headers from vtrutil library */
#include "vtr_assert.h"
#include "vtr_log.h"
#include "vtr_time.h"

/* Headers from openfpgautil library */
#include "openfpga_digest.h"

#include "openfpga_naming.h"

#include "bitstream_manager_utils.h"
#include "fabric_bitstream_utils.h"
#include "write_text_fabric_bitstream.h"

/* begin namespace openfpga */
namespace openfpga {

/********************************************************************
 * Write a configuration bit into a plain text file
 * The format depends on the type of configuration protocol
 * - Vanilla (standalone): just put down pure 0|1 bitstream
 * - Configuration chain: just put down pure 0|1 bitstream
 * - Memory bank :  <BL address> <WL address> <bit>
 * - Frame-based configuration protocol :  <address> <bit>
 *
 * Return:
 *  - 0 if succeed
 *  - 1 if critical errors occured
 *******************************************************************/
static 
int write_fabric_config_bit_to_text_file(std::fstream& fp,
                                         const BitstreamManager& bitstream_manager,
                                         const FabricBitstream& fabric_bitstream,
                                         const FabricBitId& fabric_bit,
                                         const e_config_protocol_type& config_type) {
  if (false == valid_file_stream(fp)) {
    return 1;
  }

  switch (config_type) {
  case CONFIG_MEM_STANDALONE: 
  case CONFIG_MEM_SCAN_CHAIN:
    fp << bitstream_manager.bit_value(fabric_bitstream.config_bit(fabric_bit));
    break;
  case CONFIG_MEM_MEMORY_BANK: { 
    for (const char& addr_bit : fabric_bitstream.bit_bl_address(fabric_bit)) {
      fp << addr_bit;
    }
    write_space_to_file(fp, 1);
    for (const char& addr_bit : fabric_bitstream.bit_wl_address(fabric_bit)) {
      fp << addr_bit;
    }
    write_space_to_file(fp, 1);
    fp << bitstream_manager.bit_value(fabric_bitstream.config_bit(fabric_bit));
    fp << "\n";
    break;
  }
  case CONFIG_MEM_FRAME_BASED: {
    for (const char& addr_bit : fabric_bitstream.bit_address(fabric_bit)) {
      fp << addr_bit;
    }
    write_space_to_file(fp, 1);
    fp << bitstream_manager.bit_value(fabric_bitstream.config_bit(fabric_bit));
    fp << "\n";
    break;
  }
  default:
    VTR_LOGF_ERROR(__FILE__, __LINE__,
                   "Invalid configuration protocol type!\n");
    return 1;
  }

  return 0;
}


/********************************************************************
 * Write the flatten fabric bitstream to a plain text file 
 *
 * Return:
 *  - 0 if succeed
 *  - 1 if critical errors occured
 *******************************************************************/
static 
int write_flatten_fabric_bitstream_to_text_file(std::fstream& fp,
                                                const BitstreamManager& bitstream_manager,
                                                const FabricBitstream& fabric_bitstream,
                                                const ConfigProtocol& config_protocol) {
  int status = 0;
  for (const FabricBitId& fabric_bit : fabric_bitstream.bits()) {
    status = write_fabric_config_bit_to_text_file(fp, bitstream_manager,
                                                  fabric_bitstream,
                                                  fabric_bit,
                                                  config_protocol.type());
    if (1 == status) {
      return status;
    }
  }

  return status;
}

/********************************************************************
 * Write the fabric bitstream fitting a configuration chain protocol
 * to a plain text file 
 *
 * Return:
 *  - 0 if succeed
 *  - 1 if critical errors occured
 *******************************************************************/
static 
int write_config_chain_fabric_bitstream_to_text_file(std::fstream& fp,
                                                     const BitstreamManager& bitstream_manager,
                                                     const FabricBitstream& fabric_bitstream) {
  int status = 0;

  size_t regional_bitstream_max_size = find_fabric_regional_bitstream_max_size(fabric_bitstream);
  ConfigChainFabricBitstream regional_bitstreams = build_config_chain_fabric_bitstream_by_region(bitstream_manager, fabric_bitstream);

  for (size_t ibit = 0; ibit < regional_bitstream_max_size; ++ibit) { 
    for (const auto& region_bitstream : regional_bitstreams) {
      fp << region_bitstream[ibit];
    }
    fp << std::endl;
  }

  return status;
}

/********************************************************************
 * Write the fabric bitstream fitting a memory bank protocol 
 * to a plain text file 
 *
 * Return:
 *  - 0 if succeed
 *  - 1 if critical errors occured
 *******************************************************************/
static 
int write_memory_bank_fabric_bitstream_to_text_file(std::fstream& fp,
                                                     const FabricBitstream& fabric_bitstream) {
  int status = 0;

  MemoryBankFabricBitstream fabric_bits_by_addr = build_memory_bank_fabric_bitstream_by_address(fabric_bitstream);

  for (const auto& addr_din_pair : fabric_bits_by_addr) {
    /* Write BL address code */
    fp << addr_din_pair.first.first;
    fp << " ";

    /* Write WL address code */
    fp << addr_din_pair.first.second;
    fp << " ";

    /* Write data input */
    for (const bool& din_value : addr_din_pair.second) {
      fp << din_value;
    }
    fp << std::endl;
  }

  return status;
}

/********************************************************************
 * Write the fabric bitstream fitting a frame-based protocol 
 * to a plain text file 
 *
 * Return:
 *  - 0 if succeed
 *  - 1 if critical errors occured
 *******************************************************************/
static 
int write_frame_based_fabric_bitstream_to_text_file(std::fstream& fp,
                                                    const FabricBitstream& fabric_bitstream) {
  int status = 0;

  FrameFabricBitstream fabric_bits_by_addr = build_frame_based_fabric_bitstream_by_address(fabric_bitstream);

  for (const auto& addr_din_pair : fabric_bits_by_addr) {
    /* Write address code */
    fp << addr_din_pair.first;
    fp << " ";

    /* Write data input */
    for (const bool& din_value : addr_din_pair.second) {
      fp << din_value;
    }
    fp << std::endl;
  }

  return status;
}

/********************************************************************
 * Write the fabric bitstream to a plain text file 
 * Notes: 
 *   - This is the final bitstream which is loadable to the FPGA fabric
 *     (Verilog netlists etc.)
 *   - Do NOT include any comments or other characters that the 0|1 bitstream content
 *     in this file
 *
 * Return:
 *  - 0 if succeed
 *  - 1 if critical errors occured
 *******************************************************************/
int write_fabric_bitstream_to_text_file(const BitstreamManager& bitstream_manager,
                                        const FabricBitstream& fabric_bitstream,
                                        const ConfigProtocol& config_protocol,
                                        const std::string& fname,
                                        const bool& verbose) {
  /* Ensure that we have a valid file name */
  if (true == fname.empty()) {
    VTR_LOG_ERROR("Received empty file name to output bitstream!\n\tPlease specify a valid file name.\n");
  }

  std::string timer_message = std::string("Write ") + std::to_string(fabric_bitstream.num_bits()) + std::string(" fabric bitstream into plain text file '") + fname + std::string("'");
  vtr::ScopedStartFinishTimer timer(timer_message);

  /* Create the file stream */
  std::fstream fp;
  fp.open(fname, std::fstream::out | std::fstream::trunc);

  check_file_stream(fname.c_str(), fp);

  /* Output fabric bitstream to the file */
  int status = 0;
  switch (config_protocol.type()) {
  case CONFIG_MEM_STANDALONE: 
    status = write_flatten_fabric_bitstream_to_text_file(fp,
                                                         bitstream_manager,
                                                         fabric_bitstream,
                                                         config_protocol);
    break;
  case CONFIG_MEM_SCAN_CHAIN:
    status = write_config_chain_fabric_bitstream_to_text_file(fp,
                                                              bitstream_manager,
                                                              fabric_bitstream);
    break;
  case CONFIG_MEM_MEMORY_BANK: 
    status = write_memory_bank_fabric_bitstream_to_text_file(fp,
                                                             fabric_bitstream);
    break;
  case CONFIG_MEM_FRAME_BASED:
    status = write_frame_based_fabric_bitstream_to_text_file(fp,
                                                             fabric_bitstream);
    break;
  default:
    VTR_LOGF_ERROR(__FILE__, __LINE__,
                   "Invalid configuration protocol type!\n");
    status = 1;
  }


  /* Print an end to the file here */
  fp << std::endl;

  /* Close file handler */
  fp.close();

  VTR_LOGV(verbose,
           "Outputted %lu configuration bits to plain text file: %s\n",
           fabric_bitstream.bits().size(),
           fname.c_str());

  return status;
}

} /* end namespace openfpga */
