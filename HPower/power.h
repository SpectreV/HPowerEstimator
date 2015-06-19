/*
 * power.h
 *
 *  Created on: 19 Feb, 2014
 *      Author: liang
 */

#ifndef POWER_H_
#define POWER_H_

#include <assert.h>
#include <string.h>

#include "read_xml_util.h"
#include "vpr_types.h"
#include "physical_types.h"

/* Global variables */

extern float grid_logic_tile_area;
extern float ipin_mux_trans_size;

/* external-to-complex block nets in the user netlist */
extern int num_nets;
extern struct s_net *clb_net;

/* blocks in the user netlist */
extern int num_blocks;
extern struct s_block *block;

/********************************************************************
 Physical FPGA architecture globals
 *********************************************************************/

/* x and y dimensions of the FPGA itself, the core of the FPGA is from [1..nx][1..ny], the I/Os form a perimeter surrounding the core */
extern int nx, ny;
extern struct s_grid_tile **grid; /* FPGA complex blocks grid [0..nx+1][0..ny+1] */

/* Special pointers to identify special blocks on an FPGA: I/Os, unused, and default */
extern t_type_ptr IO_TYPE;
extern t_type_ptr EMPTY_TYPE;
extern t_type_ptr FILL_TYPE;

/* type_descriptors are blocks that can be moved by the placer
 such as: I/Os, CLBs, memories, multipliers, etc
 Different types of physical block are contained in type descriptors
 */
extern int num_types;
extern struct s_type_descriptor *type_descriptors;

/* Default prefix string for output files */
extern char *OutFilePrefix;

/* name of the blif circuit */
extern char *blif_circuit_name;

/* Default area of a 1x1 logic tile (excludes routing) on the FPGA */
extern float grid_logic_tile_area;

/* Area of a mux transistor for the input connection block */
extern float ipin_mux_trans_size;

/*******************************************************************
 Packing related globals
 ********************************************************************/

/* Netlist description data structures. */

/* User netlist information */
extern int num_logical_nets, num_logical_blocks, num_saved_logical_blocks,
		num_saved_logical_nets;
extern int num_p_inputs, num_p_outputs;
extern struct s_net *vpack_net, *saved_logical_nets;
extern struct s_logical_block *logical_block, *saved_logical_blocks;
extern struct s_subckt *subckt;

/* primiary inputs removed from circuit */
extern struct s_linked_vptr *circuit_p_io_removed;

/* Relationship between external-to-complex block nets and internal-to-complex block nets */
extern int *clb_to_vpack_net_mapping; /* [0..num_clb_nets - 1] */
extern int *vpack_to_clb_net_mapping; /* [0..num_vpack_nets - 1] */

/* Number in original netlist, before FF packing. */
extern int num_luts, num_latches, num_subckts;

/*******************************************************************
 Routing related globals
 ********************************************************************/

/* chan_width_x is the x-directed channel; i.e. between rows */
extern int *chan_width_x, *chan_width_y; /* numerical form */

/* [0..num_nets-1] of linked list start pointers.  Defines the routing.  */
extern struct s_trace **trace_head, **trace_tail;

/* Structures to define the routing architecture of the FPGA.           */
extern int num_rr_nodes;
extern t_rr_node *rr_node; /* [0..num_rr_nodes-1]          */
extern int num_rr_indexed_data;
extern t_rr_indexed_data *rr_indexed_data; /* [0 .. num_rr_indexed_data-1] */
extern t_ivec ***rr_node_indices;
extern int **net_rr_terminals; /* [0..num_nets-1][0..num_pins-1] */
extern struct s_switch_inf *switch_inf; /* [0..det_routing_arch.num_switch-1] */
extern int **rr_blk_source; /* [0..num_blocks-1][0..num_class-1] */

extern t_model *user_models, *library_models;
extern t_arch Arch;
extern int TimingEnabled;
extern struct s_det_routing_arch *RoutingArch;
extern struct s_timing_inf * Timing;
/* End of global variables */

extern int fixed_chan_width;
extern boolean power_gating;

typedef struct s_net_activity {
	float probability;

	/* Transition density - average # of 1-to-0 and 0-to-1 transitions per clock cycle
	 * For example, a clock would have density = 2
	 */
	float density;
} t_net_activity;

extern t_net_activity * blif_net_activity;
extern t_net_activity * net_activity;
extern float T_critical_path;

typedef enum e_power_instance_type {
	SUM, IGNORE, MACRO, MODEL, UNKNOWN
} t_power_type;

typedef struct s_mux_power {
	int mux_size;
	float at_freq;
	float dynamic_power;
	float static_power;
} t_mux_power;

typedef struct s_buf_power {
	float at_freq;
	float dynamic_power;
	float static_power;
} t_buf_power;

typedef struct s_wire_info {
	int length;
	float C_wire;
} t_wire_info;

typedef struct s_mem_power {
	float static_power;
	float write_energy;
} t_mem_power;

typedef enum e_switch_type {
	MUX_BASED, PASS_TRANS_BASED
} t_switch_type;

typedef struct s_power_descriptor {
	int num_input_pins;
	int num_output_pins;
	float at_freq;
	union dynamic_power {
		float average;
		float *input_coeff;
	} dynamic_power;
	float static_power;
} t_power_descriptor;

typedef struct s_general_power_lib {
	t_mux_power *mux_power;
	t_buf_power *buffer_power;
	t_wire_info *wire_info;
	t_mem_power *mem_power;
	float Vdd;
} t_general_power_lib;

typedef struct s_route_power_lib {
	t_power_type route_power_type;
	t_power_descriptor *cb_power;
	t_power_descriptor *sb_power;
	t_switch_type switch_type;
} t_route_power_lib;


typedef struct s_pb_power {
	char *name;
	t_power_type type;
	t_power_descriptor *power_descriptor;
	int num_children_pb_types;
	struct s_pb_power *children, *parent;
} t_pb_power;

typedef struct s_power_usage {
	float dynamic;
	float leakage;
} t_power_usage;

void ProcessPbPower(ezxml_t Node, t_pb_power *instance,
		t_pb_power *parent);
void ProcessPbPowerLibrary(ezxml_t Node);
void ProcessGeneralPowerLibrary(ezxml_t Node);
void ProcessRoutePowerLibrary(ezxml_t Node);

void power_estimation(char *power_library);

#endif /* POWER_H_ */
