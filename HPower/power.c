/*
 * power.c
 *
 *  Created on: 21 Feb, 2014
 *      Author: liang
 */

#include "power.h"
#include <assert.h>
#include <string.h>
#include <time.h>
#include "util.h"

t_general_power_lib *general_power_lib = NULL;
t_route_power_lib *route_power_lib = NULL;
t_pb_power *root_pb_types = NULL;
int num_root_pb_types;

t_net_activity * blif_net_activity = NULL;
t_net_activity * net_activity = NULL;

const float random_input_sa = 0.25;

int logic_conf_bits = 0, num_muxes = 0, sb_num_muxes = 0, cb_num_muxes = 0, num_buffers = 0;
float primitive_power = 0, inter_power = 0, avg_sa = 0.;
double avg_ipin_fanin = 0.0, avg_chan_fanin;
int num_ipin = 0, num_chan = 0;

boolean **power_gated_block, **power_gated_chanx, **power_gated_chany, **power_gated_sb;

void power_complex_block(t_power_usage *complex_block_power, t_power_usage *mem_power,
		boolean power_gating);
float pin_dens(t_pb * pb, t_pb_graph_pin * pin);
float pin_prob(t_pb * pb, t_pb_graph_pin * pin);
void power_pb(t_power_usage *complex_block_power, t_power_usage *mem_power, t_pb *pb,
		t_pb_graph_node *pb_node, t_pb_power pb_power);
void power_routing(t_power_usage *routing_power, t_power_usage *mem_power, boolean power_gating);
void power_zero_usage(t_power_usage *power_usage);
void read_power_lib(char *power_library);
static void power_gating_handling(boolean power_gating);
static int num_default_muxes(int input_size, int default_mux_size);
static boolean power_gated_cblock(t_rr_node *node);
static boolean power_gated_sblock(t_rr_node *node);
void my_atof_1D(INOUTP float *matrix, INP int max_i, INP const char *instring);

void power_estimation(char *power_library) {

	t_power_usage routing_power, complex_block_power, mem_power;

	float p_routing, p_clb, p_mem, p_total, p_dyn, p_stat;

	clock_t start, end;

	read_power_lib(power_library);

	power_zero_usage(&routing_power);
	power_zero_usage(&complex_block_power);
	power_zero_usage(&mem_power);

	start = clock();

	power_gating_handling(power_gating);

	power_routing(&routing_power, &mem_power, power_gating);
	power_complex_block(&complex_block_power, &mem_power, power_gating);
	end = clock();
	printf("\nPower estimation took %f senconds.\n", (end - start) / 1e6);
	printf("\t\tTotal(mW)\tRouting(mW)\tLogic(mW)\tRAM(mW)\n");

	printf("Average fanin of ipin: %.5f, Average fanin of chan: %.5f\n", avg_ipin_fanin / num_ipin,
			avg_chan_fanin / num_chan);
	printf("Num # of ipin %d, num # of chans %d\n", num_ipin, num_chan);

	p_routing = 1e3 * (routing_power.dynamic + routing_power.leakage);
	p_clb = 1e3 * (complex_block_power.dynamic + complex_block_power.leakage);
	p_mem = 1e3 * (mem_power.dynamic + mem_power.leakage);
	p_stat = (routing_power.leakage + complex_block_power.leakage) * 1e3;
	p_dyn = (routing_power.dynamic + complex_block_power.dynamic) * 1e3;
	p_total = p_routing + p_clb;
	printf("\t\t%.2f\t\t%.2f\t\t%.2f\t\t%.2f\n", p_total, p_routing, p_clb, p_mem);
	printf("Dynamic\t\t%.2f\t\t%.2f\t\t%.2f\n", p_dyn, routing_power.dynamic * 1e3,
			complex_block_power.dynamic * 1e3);
	printf("Static\t\t%.2f\t\t%.2f\t\t%.2f\n", p_stat, routing_power.leakage * 1e3,
			complex_block_power.leakage * 1e3);

}

void ProcessPbPower(ezxml_t Node, t_pb_power *pb_power, t_pb_power *parent) {
	const char *Prop;
	ezxml_t Cur, Prev;
	int num_children_types;
	int i, j;

	t_pb_power *child;

	CheckElement(Node, "pb_power");

	Prop = FindProperty(Node, "name", TRUE);
	pb_power->name = my_strdup(Prop);
	ezxml_set_attr(Node, "name", NULL);

	Prop = FindProperty(Node, "power_method", TRUE);
	ezxml_set_attr(Node, "power_method", NULL);

	pb_power->parent = parent;

	if (strcmp(Prop, "sum") == 0) {
		pb_power->type = SUM;

		i = 0;
		num_children_types = CountChildren(Node, "pb_power", 1);
		pb_power->children = (t_pb_power *) my_malloc(num_children_types * sizeof(t_pb_power));
		pb_power->num_children_pb_types = num_children_types;

		Cur = Node->child;
		while (Cur) {
			ProcessPbPower(Cur, &(pb_power->children[i]), pb_power);
			i++;
			Prev = Cur;
			Cur = Cur->next;
			FreeNode(Prev);
		}
		assert(i == num_children_types);
	} else if (strcmp(Prop, "ignore") == 0) {
		pb_power->type = IGNORE;
		pb_power->children = NULL;
		pb_power->num_children_pb_types = 0;

	} else if (strcmp(Prop, "macro") == 0) {
		pb_power->type = MACRO;
		pb_power->children = NULL;
		pb_power->num_children_pb_types = 0;

		pb_power->power_descriptor = (t_power_descriptor *) my_malloc(sizeof(t_power_descriptor));

		pb_power->power_descriptor->num_input_pins = GetIntProperty(Node, "num_input", TRUE, 0);
		pb_power->power_descriptor->num_output_pins = GetIntProperty(Node, "num_output", TRUE, 0);

		Cur = ezxml_child(Node, "dynamic_power");
		pb_power->power_descriptor->at_freq = GetFloatProperty(Cur, "at_freq", TRUE, 0);
		Prop = Cur->txt;
		pb_power->power_descriptor->dynamic_power.average = atof(Prop);
		ezxml_set_txt(Cur, "");
		FreeNode(Cur);

		Cur = ezxml_child(Node, "static_power");
		Prop = Cur->txt;
		pb_power->power_descriptor->static_power = atof(Prop);
		ezxml_set_txt(Cur, "");
		FreeNode(Cur);

	} else if (strcmp(Prop, "model") == 0) {
		pb_power->type = MODEL;
		pb_power->children = NULL;
		pb_power->num_children_pb_types = 0;

		pb_power->power_descriptor = (t_power_descriptor *) my_malloc(sizeof(t_power_descriptor));

		pb_power->power_descriptor->num_input_pins = GetIntProperty(Node, "num_input", TRUE, 0);
		pb_power->power_descriptor->num_output_pins = GetIntProperty(Node, "num_output", TRUE, 0);

		Cur = ezxml_child(Node, "dynamic_power");
		pb_power->power_descriptor->at_freq = GetFloatProperty(Cur, "at_freq", TRUE, 0);
		pb_power->power_descriptor->dynamic_power.input_coeff = (float *) my_malloc(
				sizeof(float) * pb_power->power_descriptor->num_input_pins);
		Prop = Cur->txt;
		my_atof_1D(pb_power->power_descriptor->dynamic_power.input_coeff,
				pb_power->power_descriptor->num_input_pins, Prop);
		ezxml_set_txt(Cur, "");
		FreeNode(Cur);

		Cur = ezxml_child(Node, "static_power");
		Prop = Cur->txt;
		pb_power->power_descriptor->static_power = atof(Prop);
		ezxml_set_txt(Cur, "");
		FreeNode(Cur);

	} else {
		printf("Undefined power method '%s'. Please specify 'ignore', 'sum', 'model' or 'macro'\n",
				Prop);
	}

}

void ProcessRoutePowerLibrary(ezxml_t Node) {
	ezxml_t CurNode;
	const char *Prop;

	route_power_lib = (t_general_power_lib *) my_malloc(sizeof(t_route_power_lib));

	CheckElement(Node, "route_power_lib");

	route_power_lib->switch_type = MUX_BASED;
	Prop = FindProperty(Node, "switch_type", FALSE);
	ezxml_set_attr(Node, "switch_type", NULL);
	if (strcmp(Prop, "mux") == 0)
		route_power_lib->switch_type = MUX_BASED;
	else if (strcmp(Prop, "pass_trans") == 0)
		route_power_lib->switch_type = PASS_TRANS_BASED;
	else {
		printf("Undefined routing switch type '%s'. Please specify 'mux' or 'pass_trans'\n", Prop);
		exit(1);
	}

	Prop = FindProperty(Node, "power_method", FALSE);
	ezxml_set_attr(Node, "power_method", NULL);
	if (strcmp(Prop, "sum") == 0)
		route_power_lib->route_power_type = SUM;
	else if (strcmp(Prop, "macro") == 0)
		route_power_lib->route_power_type = MACRO;
	else {
		printf("Undefined routing power method '%s'. Please specify 'sum' or 'macro'\n", Prop);
		route_power_lib->route_power_type = IGNORE;
	}

	CurNode = ezxml_child(Node, "c_block");
	route_power_lib->cb_power = (t_power_descriptor *) my_malloc(sizeof(t_power_descriptor));
	ProcessCBSB(CurNode, "c_block");
	FreeNode(CurNode);

	CurNode = ezxml_child(Node, "s_block");
	route_power_lib->sb_power = (t_power_descriptor *) my_malloc(sizeof(t_power_descriptor));
	ProcessCBSB(CurNode, "s_block");
	FreeNode(CurNode);
}

void ProcessCBSB(ezxml_t Node, const char* type) {
	ezxml_t Cur;
	const char* Prop;
	t_power_descriptor *power_descriptor;

	if (strcmp(type, "c_block") == 0) {
		power_descriptor = route_power_lib->cb_power;
	} else if (strcmp(type, "s_block") == 0) {
		power_descriptor = route_power_lib->sb_power;
	}

	if (route_power_lib->route_power_type == SUM) {
		free(power_descriptor);
	} else if (route_power_lib->route_power_type == MACRO) {
		Cur = ezxml_child(Node, "dynamic_power");
		power_descriptor->at_freq = GetFloatProperty(Cur, "at_freq", TRUE, 0);
		Prop = Cur->txt;
		power_descriptor->dynamic_power.average = atof(Prop);
		ezxml_set_txt(Cur, "");
		FreeNode(Cur);

		Cur = ezxml_child(Node, "static_power");
		Prop = Cur->txt;
		power_descriptor->static_power = atof(Prop);
		ezxml_set_txt(Cur, "");
		FreeNode(Cur);
	} else {
		printf("Power method not supported: '%s'\n", Prop);
		exit(1);
	}
}

void ProcessGeneralPowerLibrary(ezxml_t Node) {
	ezxml_t CurNode;
	const char *Prop;

	general_power_lib = (t_general_power_lib *) my_malloc(sizeof(t_general_power_lib));

	CheckElement(Node, "general_power_lib");
	general_power_lib->Vdd = GetFloatProperty(Node, "Vdd", TRUE, 0);

	general_power_lib->wire_info = (t_wire_info *) my_malloc(sizeof(t_wire_info));
	general_power_lib->mux_power = (t_mux_power *) my_malloc(sizeof(t_mem_power));
	general_power_lib->buffer_power = (t_buf_power *) my_malloc(sizeof(t_buf_power));
	general_power_lib->mem_power = (t_mem_power *) my_malloc(sizeof(t_mem_power));

	CurNode = FindElement(Node, "route_segment", TRUE);
	general_power_lib->wire_info->length = GetIntProperty(CurNode, "length", TRUE, 0);
	general_power_lib->wire_info->C_wire = GetFloatProperty(CurNode, "C_wire", TRUE, 0);
	FreeNode(CurNode);

	CurNode = FindElement(Node, "mux_power", TRUE);
	general_power_lib->mux_power->mux_size = GetIntProperty(CurNode, "size", TRUE, 0);
	general_power_lib->mux_power->at_freq = GetFloatProperty(CurNode, "at_freq", TRUE, 0);
	general_power_lib->mux_power->dynamic_power = GetFloatProperty(CurNode, "dynamic", TRUE, 0);
	general_power_lib->mux_power->static_power = GetFloatProperty(CurNode, "static", TRUE, 0);
	FreeNode(CurNode);

	CurNode = FindElement(Node, "buf_power", TRUE);
	general_power_lib->buffer_power->at_freq = GetFloatProperty(CurNode, "at_freq", TRUE, 0);
	general_power_lib->buffer_power->dynamic_power = GetFloatProperty(CurNode, "dynamic", TRUE, 0);
	general_power_lib->buffer_power->static_power = GetFloatProperty(CurNode, "static", TRUE, 0);
	FreeNode(CurNode);

	CurNode = FindElement(Node, "mem_power", TRUE);
	general_power_lib->mem_power->write_energy = GetFloatProperty(CurNode, "write_energy", TRUE, 0);
	general_power_lib->mem_power->static_power = GetFloatProperty(CurNode, "static", TRUE, 0);
	FreeNode(CurNode);

}

void ProcessPbPowerLibrary(ezxml_t Node) {

	ezxml_t CurType, Prev;
	int i;

	t_pb_power *instance;

	num_root_pb_types = CountChildren(Node, "pb_power", 1);

	root_pb_types = (t_pb_power *) my_malloc(num_root_pb_types * sizeof(t_pb_power));

	CurType = Node->child;
	i = 0;
	while (CurType) {
		instance = &(root_pb_types[i++]);

		ProcessPbPower(CurType, instance, NULL);

		Prev = CurType;
		CurType = CurType->next;
		FreeNode(Prev);
	}

}

void read_power_lib(char *power_library) {
	ezxml_t Cur, Next;
	const char *Prop;

	Cur = ezxml_parse_file(power_library);

	if (NULL == Cur) {
		printf("Unable to load power file '%s'.\n", power_library);
		exit(1);
	}

	CheckElement(Cur, "power_lib");
	Prop = FindProperty(Cur, "type", FALSE);
	ezxml_set_attr(Cur, "type", NULL);

	Next = FindElement(Cur, "pb_power_lib", TRUE);
	ProcessPbPowerLibrary(Next);
	FreeNode(Next);

	Next = FindElement(Cur, "route_power_lib", TRUE);
	ProcessRoutePowerLibrary(Next);
	FreeNode(Next);

	Next = FindElement(Cur, "general_power_lib", TRUE);
	ProcessGeneralPowerLibrary(Next);
	FreeNode(Next);

	FreeNode(Cur);

	printf("Power library '%s' successfully loaded!\n", power_library);
}

void power_complex_block(t_power_usage *complex_block_power, t_power_usage *mem_power,
		boolean power_gating) {
	int x, y, z;
	int i;
	float temp_dyn, temp_leakage;

	boolean flag = FALSE;

	/* Loop through all grid locations */
	temp_dyn = temp_leakage = 0;

	for (x = 0; x < nx + 2; x++) {
		for (y = 0; y < ny + 2; y++) {

			if ((grid[x][y].offset != 0) || (grid[x][y].type == EMPTY_TYPE)) {
				continue;
			}

			for (z = 0; z < grid[x][y].type->capacity; z++) {
				t_pb * pb = NULL;
				t_power_usage pb_power;
				i = grid[x][y].blocks[z];

				if (grid[x][y].blocks[z] != EMPTY) {
					pb = block[grid[x][y].blocks[z]].pb;
				}

				flag = FALSE;

				/*printf("Dealing with Type '%s'.\n", IO_TYPE->name);*/
				if (power_gating && power_gated_block[x][y])
					continue;
				for (i = 0; i < num_root_pb_types; ++i) {
					if (strcmp(root_pb_types[i].name, grid[x][y].type->name) == 0) {
						power_pb(complex_block_power, mem_power, pb, grid[x][y].type->pb_graph_head,
								root_pb_types[i]);
						flag = TRUE;

						break;
					}
				}
				/*printf(
				 "power usage of blk (%d, %d, %d), dynamic: %.2e, leakage: %.2e\n",
				 x, y, x, complex_block_power->dynamic - temp_dyn,
				 complex_block_power->leakage - temp_leakage);
				 */temp_dyn = complex_block_power->dynamic;
				temp_leakage = complex_block_power->leakage;
				if (flag == FALSE) {
					printf(
							"Cannot find corresponding complex block information in the power library for pb %s at (%d %d %d), ignoring this pb\n",
							pb->name, x, y, z);
				}
				/* Calculate power of this CLB */

			}
		}
	}
	printf("Total # of configuration bits in logic: %d\n", logic_conf_bits);
	printf("Total primitives power: %.2e\n", primitive_power);
	printf("Total interconnect power: %.2e\n", inter_power);
	printf("Total # of logic_muxes: %d\n", num_muxes);
	printf("Average SA of muxes: %.5f\n", avg_sa / num_muxes);
}

float pin_dens(t_pb * pb, t_pb_graph_pin * pin) {
	float density = 0.;

	if (pb) {
		int net_num;
		net_num = pb->rr_graph[pin->pin_count_in_cluster].net_num;

		if (net_num != OPEN) {
			density = blif_net_activity[net_num].density;
		}
	}

	return density;
}

float pin_prob(t_pb * pb, t_pb_graph_pin * pin) {
	/* Assumed pull-up on unused interconnect */
	float prob = 1.;

	if (pb) {
		int net_num;
		net_num = pb->rr_graph[pin->pin_count_in_cluster].net_num;

		if (net_num != OPEN) {
			prob = blif_net_activity[net_num].probability;
		}
	}

	return prob;
}

void power_pb(t_power_usage *complex_block_power, t_power_usage *mem_power, t_pb *pb,
		t_pb_graph_node *pb_node, t_pb_power pb_power) {

	const t_pb_type * pb_type = pb_node->pb_type;
	int pb_type_idx, pb_mode, pb_idx, interc_idx, out_port_idx, in_port_idx;
	int i, pin_idx, port_idx, pin_num;
	/*float *input_probabilities, *input_densities;*/
	float average_sa, average_sa_1mux;
	boolean defined;
	t_pb_graph_pin *pin;
	t_interconnect *interconnect;
	int num_inputs, default_mux_size;
	int clock_flag, mux_flag;
	int num_level0_muxes;

	if (pb_power.type == IGNORE)
		return;

	if (pb_power.type == MACRO) {
		/* This is a leaf node, which is a primitive (lut, ff, etc) */

		if (!pb) {
			complex_block_power->leakage += pb_power.power_descriptor->static_power;
			primitive_power += pb_power.power_descriptor->static_power;
			return;
		}

		average_sa = 1;
		pin_num = 0;
		for (port_idx = 0; port_idx < pb_node->num_input_ports; ++port_idx) {
			for (pin_idx = 0; pin_idx < pb_node->num_input_pins[port_idx]; ++pin_idx) {
				pin = &pb_node->input_pins[port_idx][pin_idx];
				average_sa *= 1 - pin_dens(pb, pin) / 2;
				pin_num++;
			}
		}
		/* average_sa is the probability of the circuit is silent
		 * 1 - average_sa is the probability that any input is switching*/
		average_sa = 1 - average_sa;

		complex_block_power->dynamic += average_sa
				* pb_power.power_descriptor->dynamic_power.average / T_critical_path
				/ pb_power.power_descriptor->at_freq;
		complex_block_power->leakage += pb_power.power_descriptor->static_power;
		primitive_power += average_sa * pb_power.power_descriptor->dynamic_power.average
				/ T_critical_path / pb_power.power_descriptor->at_freq
				+ pb_power.power_descriptor->static_power;

	} else if (pb_power.type == MODEL) {
		/* This is a leaf node, which is a primitive (lut, ff, etc) */

		if (!pb) {
			complex_block_power->leakage += pb_power.power_descriptor->static_power;
			primitive_power += pb_power.power_descriptor->static_power;
			return;
		}

		pin_num = 0;
		for (port_idx = 0; port_idx < pb_node->num_input_ports; ++port_idx) {
			for (pin_idx = 0; pin_idx < pb_node->num_input_pins[port_idx]; ++pin_idx) {
				pin = &pb_node->input_pins[port_idx][pin_idx];
				assert(pin_num < pb_power.power_descriptor->num_input_pins);
				complex_block_power->dynamic += pin_dens(pb, pin) / 2 / T_critical_path
						/ pb_power.power_descriptor->at_freq
						* pb_power.power_descriptor->dynamic_power.input_coeff[pin_num];
				/* P_dyn = SA_current / SA_measure * freq_current / freq_measure * Lib_dyn */
				complex_block_power->leakage += pb_power.power_descriptor->static_power;
				pin_num++;

			}
		}

	} else {
		/*This node contains children*/
		assert(pb_power.type == SUM);
		if (pb)
			pb_mode = pb->mode;
		else {
			/*make it default mode*/
			pb_mode = 0;
		}
		for (pb_type_idx = 0; pb_type_idx < pb_node->pb_type->modes[pb_mode].num_pb_type_children;
				pb_type_idx++) {
			defined = FALSE;
			for (pb_idx = 0;
					pb_idx < pb_node->pb_type->modes[pb_mode].pb_type_children[pb_type_idx].num_pb;
					pb_idx++) {
				t_pb * child_pb = NULL;
				t_pb_graph_node * child_pb_graph_node;

				if (pb && pb->child_pbs[pb_type_idx][pb_idx].name) {
					/* Child is initialized */
					child_pb = &pb->child_pbs[pb_type_idx][pb_idx];
				}
				child_pb_graph_node = &pb_node->child_pb_graph_nodes[pb_mode][pb_type_idx][pb_idx];
				for (i = 0; i < pb_power.num_children_pb_types; ++i) {
					if (strcmp(child_pb_graph_node->pb_type->name, pb_power.children[i].name)
							== 0) {
						defined = TRUE;
						power_pb(complex_block_power, mem_power, child_pb, child_pb_graph_node,
								pb_power.children[i]);
						break;
					}
				}

			}
			if (!defined) {
				printf("Pb_type %s not defined in the library, power ignored.\n",
						pb_node->pb_type->modes[pb_mode].pb_type_children[pb_type_idx].name);
			}
		}

		for (interc_idx = 0; interc_idx < pb_type->modes[pb_mode].num_interconnect; interc_idx++) {
			interconnect = &pb_type->modes[pb_mode].interconnect[interc_idx];
			switch (interconnect->type) {
			case DIRECT_INTERC:
				/* We didn't estimate direct local interconnect for now */
				continue;
			case MUX_INTERC:
			case COMPLETE_INTERC:
				/* Many-to-1, or Many-to-Many
				 * Implemented as a multiplexer for each output
				 * */
				num_inputs = 0;
				average_sa = 0;
				average_sa_1mux = 1;
				clock_flag = 0;
				mux_flag = 0;
				num_level0_muxes = 0;
				default_mux_size = general_power_lib->mux_power->mux_size;

				for (in_port_idx = 0; in_port_idx < interconnect->num_input_pb_graph_node_sets;
						in_port_idx++) {
					for (pin_idx = 0;
							pin_idx < interconnect->num_input_pb_graph_node_pins[in_port_idx];
							pin_idx++) {
						num_inputs++;
						t_pb_graph_pin * input_pin =
								interconnect->input_pb_graph_node_pins[in_port_idx][pin_idx];
						average_sa_1mux *= 1 - pin_dens(pb, input_pin) / 2;
						if (mux_flag <= default_mux_size) {
							mux_flag++;
						} else {
							mux_flag = 0;
							average_sa += 1 - average_sa_1mux;
							average_sa_1mux = 1;
							num_level0_muxes++;
						}
						if (input_pin->type == PB_PIN_CLOCK)
							clock_flag = 1;

					}
				}

				if (mux_flag != 0) {
					average_sa += 1 - average_sa_1mux;
					num_level0_muxes++;
				}

				average_sa = average_sa / num_level0_muxes;

				for (out_port_idx = 0; out_port_idx < interconnect->num_output_pb_graph_node_sets;
						out_port_idx++) {
					for (pin_idx = 0;
							pin_idx < interconnect->num_output_pb_graph_node_pins[out_port_idx];
							pin_idx++) {
						if (!clock_flag) {
							t_pb_graph_pin * output_pin =
									interconnect->output_pb_graph_node_pins[out_port_idx][pin_idx];
							complex_block_power->dynamic += num_default_muxes(num_inputs,
									default_mux_size) * general_power_lib->mux_power->dynamic_power
									/ T_critical_path / general_power_lib->mux_power->at_freq
									* average_sa;
							complex_block_power->leakage += num_default_muxes(num_inputs,
									default_mux_size) * general_power_lib->mux_power->static_power
									* (1 - average_sa);

							num_muxes += num_default_muxes(num_inputs, default_mux_size);
							avg_sa += average_sa;

							inter_power += num_default_muxes(num_inputs, default_mux_size)
									* general_power_lib->mux_power->dynamic_power / T_critical_path
									/ general_power_lib->mux_power->at_freq * average_sa
									+ num_default_muxes(num_inputs, default_mux_size)
											* general_power_lib->mux_power->static_power
											* (1 - average_sa);

							mem_power->leakage += general_power_lib->mem_power->static_power
									* num_default_muxes(num_inputs, default_mux_size)
									* ceil(log(default_mux_size) / log(2));
							logic_conf_bits += num_default_muxes(num_inputs, default_mux_size)
									* ceil(log(default_mux_size) / log(2));
						}
					}
				}

				break;
			}
		}

	}

}

void power_routing(t_power_usage *routing_power, t_power_usage *mem_power, boolean power_gating) {
	int i, rr_node_idx, net_idx, num_sb;
	t_rr_node *node, *prev, *next;
	struct s_trace *trace;
	int wire_length;
	int fan_in, edge;
	float C_wire;
	float fanin_sa;
	const t_segment_inf *seg_details;
	int *num_pin_fanin = (int *) my_malloc(num_rr_nodes * sizeof(int));
	int *num_track_fanin = (int *) my_malloc(num_rr_nodes * sizeof(int));
	float *average_fanin_sa = (float *) my_malloc(num_rr_nodes * sizeof(float));
	float temp_dyn, temp_leakage;

	int total_conf_bits, cb_fanout, sb_fanout;

	seg_details = Arch.Segments;

	t_net_activity *clb_net_activity = (t_net_activity *) my_malloc(
			sizeof(t_net_activity) * num_nets);

	for (i = 0; i < num_nets; ++i) {
		clb_net_activity[i].probability =
				blif_net_activity[clb_to_vpack_net_mapping[i]].probability;
		clb_net_activity[i].density = blif_net_activity[clb_to_vpack_net_mapping[i]].density;
	}

	for (i = 0; i < num_rr_nodes; ++i) {
		num_pin_fanin[i] = 0;
		num_track_fanin[i] = 0;
		average_fanin_sa[i] = 1.;
		rr_node[i].net_num = OPEN;
	}

	for (net_idx = 0; net_idx < num_nets; net_idx++) {
		for (trace = trace_head[net_idx]; trace != NULL; trace = trace->next) {
			node = &rr_node[trace->index];
			/*prev->net_num = net_idx;
			 if (trace->next == NULL)
			 break;
			 node = &rr_node[trace->next->index];
			 */
			node->net_num = net_idx;
			/*if ((prev->type == CHANX || prev->type == CHANY)
			 && (node->type == CHANX || node->type == CHANY))
			 chan_driven_by_chan[trace->next->index] = 1;*/
		}
	}

	for (rr_node_idx = 0; rr_node_idx < num_rr_nodes; rr_node_idx++) {
		node = &rr_node[rr_node_idx];
		if (node->net_num != OPEN) {
			net_idx = node->net_num;
			for (edge = 0; edge < node->num_edges; ++edge) {
				average_fanin_sa[node->edges[edge]] *= 1 - clb_net_activity[net_idx].density / 2.0;
			}
		}

		for (edge = 0; edge < node->num_edges; ++edge) {
			if (node->type == OPIN) {
				num_pin_fanin[node->edges[edge]]++;
			} else if (node->type == CHANX || node->type == CHANY) {
				num_track_fanin[node->edges[edge]]++;
			}
		}
	}

	float wire_power = 0, sb_power = 0, cb_power = 0;

	total_conf_bits = 0;
	temp_dyn = temp_leakage = 0;
	for (rr_node_idx = 0; rr_node_idx < num_rr_nodes; rr_node_idx++) {
		node = &rr_node[rr_node_idx];
		fanin_sa = 1 - average_fanin_sa[rr_node_idx];
		cb_fanout = sb_fanout = 0;
		fan_in = -1;
		if (route_power_lib->switch_type == MUX_BASED) {
			int default_mux_size = general_power_lib->mux_power->mux_size;
			switch (node->type) {
			case SOURCE:
			case SINK:
			case OPIN:
				break;
			case IPIN:
				if (power_gating && power_gated_cblock(node))
					break;
				num_ipin++;
				fan_in = node->fan_in;
				if (fan_in) {
					avg_ipin_fanin += fan_in;
					routing_power->dynamic += fanin_sa * general_power_lib->mux_power->dynamic_power
							* num_default_muxes(fan_in, default_mux_size)
							/ general_power_lib->mux_power->at_freq / T_critical_path;
					routing_power->leakage += general_power_lib->mux_power->static_power
							* num_default_muxes(fan_in, default_mux_size);

					cb_num_muxes += num_default_muxes(fan_in, default_mux_size);

					total_conf_bits += num_default_muxes(fan_in, default_mux_size)
							* ceil(log(default_mux_size) / log(2));
				}
				break;
			case CHANX:
			case CHANY:
				num_chan++;

				fan_in = node->fan_in;
				avg_chan_fanin += fan_in;
				wire_length = 0;
				if (node->type == CHANX) {
					wire_length = node->xhigh - node->xlow + 1;
				} else if (node->type == CHANY) {
					wire_length = node->yhigh - node->ylow + 1;
				}
				C_wire = general_power_lib->wire_info->C_wire * wire_length;

				num_sb = 0; /* Fanout switching boxes*/

				for (i = 0; i < wire_length; ++i) {
					if (seg_details->sb[i + 1]) {
						num_sb++;
					}
				}
				for (i = 0; i < node->num_edges; i++) {
					if (node->switches[i] == 0) {
						cb_fanout++;
					} else if (node->switches[i] == 1) {
					} else {
						sb_fanout++;
					}
				}
				/* Wire Power */
				routing_power->dynamic += general_power_lib->Vdd * general_power_lib->Vdd * C_wire
						/ T_critical_path * clb_net_activity[node->net_num].density / 2;
				wire_power += general_power_lib->Vdd * general_power_lib->Vdd * C_wire
						/ T_critical_path * clb_net_activity[node->net_num].density / 2;

				/* Buffer to SB */
				routing_power->dynamic += general_power_lib->buffer_power->dynamic_power
						/ T_critical_path / general_power_lib->buffer_power->at_freq
						* clb_net_activity[node->net_num].density / 2;
				routing_power->leakage += general_power_lib->buffer_power->static_power
						* (1 - clb_net_activity[node->net_num].density / 2);
				num_buffers++;

				/* Buffer to CB */
				routing_power->dynamic += general_power_lib->buffer_power->dynamic_power
						/ T_critical_path / general_power_lib->buffer_power->at_freq
						* clb_net_activity[node->net_num].density / 2;
				routing_power->leakage += general_power_lib->buffer_power->static_power
						* (1 - clb_net_activity[node->net_num].density / 2);

				num_buffers++;

				/* SB Power that drives the wire */
				if (power_gating && power_gated_sblock(node)) {
					/*Do Nothing*/
				} else {
					routing_power->dynamic += fanin_sa * general_power_lib->mux_power->dynamic_power
							* num_default_muxes(num_track_fanin[rr_node_idx], default_mux_size)
							/ general_power_lib->mux_power->at_freq / T_critical_path;
					routing_power->leakage += general_power_lib->mux_power->static_power
							* num_default_muxes(num_track_fanin[rr_node_idx], default_mux_size);
					sb_num_muxes += num_default_muxes(num_track_fanin[rr_node_idx],
							default_mux_size);
				}

				/* CB Power that drives the wire */
				if (power_gating && power_gated_cblock(node)) {
					/*Do Nothing*/
				} else {
					routing_power->dynamic += fanin_sa * general_power_lib->mux_power->dynamic_power
							* num_default_muxes(num_pin_fanin[rr_node_idx], default_mux_size)
							/ general_power_lib->mux_power->at_freq / T_critical_path;
					routing_power->leakage += (1 - fanin_sa)
							* general_power_lib->mux_power->static_power
							* num_default_muxes(num_pin_fanin[rr_node_idx], default_mux_size);
					cb_num_muxes += num_default_muxes(num_pin_fanin[rr_node_idx], default_mux_size);
				}

				int pin_fanin = num_pin_fanin[rr_node_idx], track_fanin =
						num_track_fanin[rr_node_idx];
				assert(fan_in == num_pin_fanin[rr_node_idx] + num_track_fanin[rr_node_idx]);

				break;
			default:
				break;
			}
			/*printf(
			 "power usage of rr_node #%d, dynamic: %.2e, leakage: %.2e, fanin_sa: %.5f, fanin: %d\n",
			 rr_node_idx, routing_power->dynamic - temp_dyn,
			 routing_power->leakage - temp_leakage, fanin_sa, fan_in);
			 */
			temp_dyn = routing_power->dynamic;
			temp_leakage = routing_power->leakage;
		}
	}

	free(average_fanin_sa);
	printf("Total # of configuration bits in routing: %d\n", total_conf_bits);
	printf("Total # of cb_muxes %d, Total # of sb_muxes %d, Total # of buffers %d\n", cb_num_muxes,
			sb_num_muxes, num_buffers);
	printf("Total wire power: %.2e\n", wire_power);
}

void power_zero_usage(t_power_usage *power_usage) {
	power_usage->dynamic = 0;
	power_usage->leakage = 0;
}

static int num_default_muxes(int input_size, int default_mux_size) {
	int i;

	if (input_size == 0)
		return 0;

	if (default_mux_size == 1) {
		return input_size;
	}

	i = 1;
	while (input_size > default_mux_size) {
		input_size = ceil(1.0 * input_size / default_mux_size);
		i += input_size;

	}
	return i;
}

static void power_gating_handling(boolean gating) {
	int x, y, i, net_idx;

	t_rr_node *node, *prev, *next;
	struct s_trace *trace;

	if(!gating)
		return;

	power_gated_block = (boolean **) my_malloc((nx + 2) * sizeof(boolean *));
	for (x = 0; x < nx + 2; ++x) {
		power_gated_block[x] = (boolean *) my_malloc((ny + 2) * sizeof(boolean));
		for (y = 0; y < ny + 2; ++y) {
			power_gated_block[x][y] = TRUE;
		}
	}

	power_gated_chanx = (boolean **) my_malloc((nx + 1) * sizeof(boolean *));
	for (x = 0; x < nx + 1; ++x) {
		power_gated_chanx[x] = (boolean *) my_malloc((ny + 1) * sizeof(boolean));
		for (y = 0; y < ny + 1; ++y) {
			power_gated_chanx[x][y] = TRUE;
		}
	}

	power_gated_chany = (boolean **) my_malloc((nx + 1) * sizeof(boolean *));
	for (x = 0; x < nx + 1; ++x) {
		power_gated_chany[x] = (boolean *) my_malloc((ny + 1) * sizeof(boolean));
		for (y = 0; y < ny + 1; ++y) {
			power_gated_chany[x][y] = TRUE;
		}
	}

	power_gated_sb = (boolean **) my_malloc((nx + 1) * sizeof(boolean *));
	for (x = 0; x < nx + 1; ++x) {
		power_gated_sb[x] = (boolean *) my_malloc((ny + 1) * sizeof(boolean));
		for (y = 0; y < ny + 1; ++y) {
			power_gated_sb[x][y] = TRUE;
		}
	}

	/* Power Gating */
	if (power_gating) {
		for (i = 0; i < num_blocks; ++i) {
			power_gated_block[block[i].x][block[i].y] = FALSE;
		}

		for (x = 1; x < nx + 1; ++x) {
			for (y = 0; y < ny + 1; ++y) {
				if (!power_gated_block[x][y] || !power_gated_block[x][y + 1]) {
					power_gated_chanx[x][y] = FALSE;
				}
			}
		}

		for (x = 0; x < nx + 1; ++x) {
			for (y = 1; y < ny + 1; ++y) {
				if (!power_gated_block[x][y] || !power_gated_block[x + 1][y]) {
					power_gated_chany[x][y] = FALSE;
				}
			}
		}

		for (net_idx = 0; net_idx < num_nets; net_idx++) {
			for (trace = trace_head[net_idx]; trace != NULL; trace = trace->next) {
				prev = &rr_node[trace->index];
				if (trace->next == NULL)
					break;
				node = &rr_node[trace->next->index];
				if (prev->type == CHANX) {
					/* CHANX-to-CHANY */
					if (node->type == CHANY) {
						x = node->xlow;
						y = prev->ylow;
						power_gated_sb[x][y] = FALSE;
					}
					/* CHANX-to-CHANX */
					if (node->type == CHANX && node->direction == INC_DIRECTION) {
						x = prev->xhigh;
						y = prev->yhigh;
						power_gated_sb[x][y] = FALSE;
					}
					if (node->type == CHANX && node->direction == DEC_DIRECTION) {
						x = node->xhigh;
						y = node->yhigh;
						power_gated_sb[x][y] = FALSE;
					}
				}
				if (prev->type == CHANY) {
					/* CHANY-to-CHANX */
					if (node->type == CHANX) {
						x = prev->xlow;
						y = node->ylow;
						power_gated_sb[x][y] = FALSE;
					}
					/* CHANY-to-CHANY */
					if (node->type == CHANY && node->direction == INC_DIRECTION) {
						x = prev->xhigh;
						y = prev->xhigh;
						power_gated_sb[x][y] = FALSE;
					}
					if (node->type == CHANY && node->direction == DEC_DIRECTION) {
						x = node->xhigh;
						y = node->yhigh;
						power_gated_sb[x][y] = FALSE;
					}
				}

			}
		}

		/*for (x = 0; x < nx + 1; ++x) {
		 printf("L%d|  |", power_gated_block[x][ny + 1]);
		 }
		 printf("L%d\n  |", power_gated_block[nx + 1][ny + 1]);

		 for (y = ny; y > 0; --y) {
		 for (x = 1; x < nx + 1; ++x) {
		 printf("S%d|C%d|", power_gated_sb[x - 1][y],
		 power_gated_chanx[x][y]);
		 }
		 printf("S%d|\n", power_gated_sb[nx][y]);
		 for (x = 0; x < nx + 1; ++x) {
		 printf("L%d|C%d|", power_gated_block[x][y],
		 power_gated_chany[x][y]);
		 }
		 printf("L%d\n  |", power_gated_block[nx + 1][y]);

		 }

		 for (x = 1; x < nx + 1; ++x) {
		 printf("S%d|C%d|", power_gated_sb[x - 1][0],
		 power_gated_chanx[x][0]);
		 }
		 printf("S%d|\n", power_gated_sb[nx][0]);
		 for (x = 0; x < nx + 1; ++x) {
		 printf("L%d|  |", power_gated_block[x][0]);
		 }
		 printf("L%d\n\n", power_gated_block[nx + 1][0]);*/
	}
}

static boolean power_gated_cblock(t_rr_node *node) {

	int x, y, ptc, iside, ofs;
	int chan_type, chan, seg;
	t_type_ptr type_ptr;
	boolean vert, pos_dir;

	if (node->type == IPIN) {
		x = node->xlow;
		y = node->ylow;
		ptc = node->ptc_num;
		type_ptr = grid[x][y].type;
		for (ofs = 0; ofs < type_ptr->height; ++ofs) {
			for (iside = 0; iside < 4; ++iside) {
				if (type_ptr->pinloc[ofs][iside][ptc] == 0)
					continue;
				if (iside == BOTTOM)
					y--;
				if (iside == TOP)
					y += ofs;
				if (iside == LEFT)
					x--;
				break;
			}
		}
		if (chan_type == CHANX && power_gated_chanx[x][y])
			return TRUE;
		else if (chan_type == CHANY && power_gated_chany[x][y])
			return TRUE;
		else
			return FALSE;
	}

	else {
		assert(node->type == CHANX || node->type == CHANY);

		if (node->direction == INC_DIRECTION) {
			x = node->xlow;
			y = node->ylow;
		} else {
			x = node->xhigh;
			y = node->yhigh;
		}
		if (node->type == CHANX && power_gated_chanx[x][y])
			return TRUE;
		if (node->type == CHANY && power_gated_chany[x][y])
			return TRUE;
		else
			return FALSE;
	}
}

static boolean power_gated_sblock(t_rr_node *node) {
	int x, y;

	assert(node->type == CHANX || node->type == CHANY);
	if (node->direction == INC_DIRECTION && node->type == CHANX) {
		x = node->xlow - 1;
		y = node->ylow;
	}
	if (node->direction == INC_DIRECTION && node->type == CHANY) {
		x = node->xlow;
		y = node->ylow - 1;
	} else {
		x = node->xhigh;
		y = node->yhigh;
	}
	if (node->type == CHANX && power_gated_sb[x][y])
		return TRUE;
	if (node->type == CHANY && power_gated_sb[x][y])
		return TRUE;
	else
		return FALSE;

}

void my_atof_1D(INOUTP float *matrix, INP int max_i, INP const char *instring) {
	int i;
	char *cur, *cur2, *copy, *final;

	copy = my_strdup(instring);
	final = copy;
	while (*final != '\0') {
		final++;
	}

	cur = copy;
	i = 0;
	while (cur != final) {
		while (IsWhitespace(*cur) && cur != final) {
			cur++;
		}
		if (cur == final) {
			break;
		}
		cur2 = cur;
		while (!IsWhitespace(*cur2) && cur2 != final) {
			cur2++;
		}
		*cur2 = '\0';
		assert(i < max_i);
		matrix[i++] = atof(cur);
		cur = cur2;
		*cur = ' ';
	}

	assert(i == max_i);

	free(copy);
}

