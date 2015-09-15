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

static t_general_power_lib *general_power_lib = NULL;
static t_power_instance *complex_block_instances = NULL;
static int num_root_power_instances;

t_net_activity * blif_net_activity = NULL;
t_net_activity * net_activity = NULL;

int logic_conf_bits = 0, num_muxes = 0, sb_num_muxes = 0, cb_num_muxes = 0, num_buffers = 0;
float primitive_power = 0, inter_power = 0, avg_sa = 0.;
double avg_ipin_fanin = 0.0, avg_chan_fanin;
int num_ipin = 0, num_chan = 0;

void power_complex_block(t_power_usage *complex_block_power,
		t_power_usage *mem_power);
float pin_dens(t_pb * pb, t_pb_graph_pin * pin);
float pin_prob(t_pb * pb, t_pb_graph_pin * pin);
void power_pb(t_power_usage *complex_block_power, t_power_usage *mem_power,
		t_pb *pb, t_pb_graph_node *pb_node, t_power_instance instance);
void power_routing(t_power_usage *routing_power, t_power_usage *mem_power);
void power_zero_usage(t_power_usage *power_usage);
void read_power_lib(char *power_library);
static int num_default_muxes(int input_size, int default_mux_size);

void power_estimation(char *power_library) {

	t_power_usage routing_power, complex_block_power, mem_power;

	float p_routing, p_clb, p_mem, p_total, p_dyn, p_stat;

	clock_t start, end;

	read_power_lib(power_library);

	power_zero_usage(&routing_power);
	power_zero_usage(&complex_block_power);
	power_zero_usage(&mem_power);

	start = clock();
	power_routing(&routing_power, &mem_power);
	power_complex_block(&complex_block_power, &mem_power);
	end = clock();
	printf("\nPower estimation took %f senconds.\n", (end - start) / 1e6);
	printf("\t\tTotal(mW)\tRouting(mW)\tLogic(mW)\n");

	printf("Average fanin of ipin: %.5f, Average fanin of chan: %.5f\n", avg_ipin_fanin / num_ipin, avg_chan_fanin / num_chan);
	printf("Num # of ipin %d, num # of chans %d\n", num_ipin, num_chan);

	p_routing = 1e3 * (routing_power.dynamic + routing_power.leakage);
	p_clb = 1e3 * (complex_block_power.dynamic + complex_block_power.leakage);
	p_mem = 1e3 * (mem_power.dynamic + mem_power.leakage);
	p_stat = (routing_power.leakage + complex_block_power.leakage) * 1e3;
	p_dyn = (routing_power.dynamic + complex_block_power.dynamic) * 1e3;
	p_total = p_routing + p_clb;
	printf("\t\t%.2f\t\t%.2f\t\t%.2f\n", p_total, p_routing, p_clb
			);
	printf("Dynamic\t\t%.2f\t\t%.2f\t\t%.2f\n", p_dyn / p_total,
			routing_power.dynamic / p_routing * 1e3 ,
			complex_block_power.dynamic / p_clb * 1e3 );
	printf("Static\t\t%.2f\t\t%.2f\t\t%.2f\n", p_stat / p_total,
			routing_power.leakage / p_routing * 1e3 ,
			complex_block_power.leakage / p_clb * 1e3 );

}

void ProcessPbPower(ezxml_t Node, t_power_instance *instance,
		t_power_instance *parent) {
	const char *Prop;
	ezxml_t Cur, Prev;
	int num_children_types;
	int i, j;

	t_power_instance *child;

	CheckElement(Node, "pb_power");

	Prop = FindProperty(Node, "name", TRUE);
	instance->name = my_strdup(Prop);
	ezxml_set_attr(Node, "name", NULL);

	Prop = FindProperty(Node, "power_method", TRUE);
	ezxml_set_attr(Node, "power_method", NULL);

	instance->parent = parent;

	if (parent)
		instance->depth = parent->depth + 1;
	else
		instance->depth = 0;

	instance->num_conf_bits = 0;

	if (strcmp(Prop, "sum_of_children") == 0) {
		instance->type = SUM;

		i = 0;
		num_children_types = CountChildren(Node, "pb_power", 1);
		instance->children = (t_power_instance *) my_malloc(
				num_children_types * sizeof(t_power_instance));
		instance->num_children_types = num_children_types;
		instance->index = -1;

		Cur = Node->child;
		while (Cur) {
			ProcessPbPower(Cur, &(instance->children[i]), instance);
			i++;
			Prev = Cur;
			Cur = Cur->next;
			FreeNode(Prev);
		}
		assert(i == num_children_types);
	} else if (strcmp(Prop, "ignore") == 0) {
		instance->type = IGNORE;
		instance->children = NULL;
		instance->num_children_types = 0;
		instance->index = -1;

	} else if (strcmp(Prop, "macro") == 0) {
		instance->type = MACRO;
		instance->children = NULL;
		instance->num_children_types = 0;
		instance->index = -1;

		instance->input_size = GetIntProperty(Node, "num_input", TRUE, 0);
		instance->output_size = GetIntProperty(Node, "num_output", TRUE, 0);
		instance->num_conf_bits = GetIntProperty(Node, "conf_bits_num", TRUE,
				0);

		Cur = ezxml_child(Node, "dynamic_power_avg");
		instance->at_freq = GetFloatProperty(Cur, "at_freq", TRUE, 0);
		Prop = Cur->txt;
		instance->dynamic_power_avg = atof(Prop);
		ezxml_set_txt(Cur, "");
		FreeNode(Cur);

		Cur = ezxml_child(Node, "static_power_avg");
		Prop = Cur->txt;
		instance->static_power_avg = atof(Prop);
		ezxml_set_txt(Cur, "");
		FreeNode(Cur);

	} else {
		printf(
				"Undefined power method '%s'. Please specify 'ignore', 'sum_of_children' or 'macro'\n",
				Prop);
	}

}

void ProcessGeneralPowerLibrary(ezxml_t Node) {
	ezxml_t CurNode;
	const char *Prop;

	CheckElement(Node, "general_power_lib");
	general_power_lib->Vdd = GetFloatProperty(Node, "Vdd", TRUE, 0);

	general_power_lib->wire_info = (t_wire_info *) my_malloc(
			sizeof(t_wire_info));
	general_power_lib->mux_power = (t_mux_power *) my_malloc(
			sizeof(t_mem_power));
	general_power_lib->buffer_power = (t_buf_power *) my_malloc(
			sizeof(t_buf_power));
	/*general_power_lib->sb_switch_power = (t_switch_power *) my_malloc(
	 sizeof(t_switch_power));
	 general_power_lib->cb_switch_power = (t_switch_power *) my_malloc(
	 sizeof(t_switch_power));*/
	general_power_lib->mem_power = (t_mem_power *) my_malloc(
			sizeof(t_mem_power));

	CurNode = FindElement(Node, "route_segment", TRUE);
	/*Prop = FindProperty(CurNode, "length", TRUE);*/
	general_power_lib->wire_info->length = GetIntProperty(CurNode, "length",
			TRUE, 0);
	/*Prop = FindProperty(CurNode, "C_wire", TRUE);*/
	general_power_lib->wire_info->C_wire = GetFloatProperty(CurNode, "C_wire",
			TRUE, 0);
	FreeNode(CurNode);

	CurNode = FindElement(Node, "mux_power", TRUE);
	/*Prop = FindProperty(CurNode, "size", TRUE);*/
	general_power_lib->mux_power->mux_size = GetIntProperty(CurNode, "size",
			TRUE, 0);
	/*Prop = FindProperty(CurNode, "at_freq", TRUE);*/
	general_power_lib->mux_power->at_freq = GetFloatProperty(CurNode, "at_freq",
			TRUE, 0);
	/*Prop = FindProperty(CurNode, "dynamic", TRUE);*/
	general_power_lib->mux_power->dynamic_power = GetFloatProperty(CurNode,
			"dynamic", TRUE, 0);
	/*Prop = FindProperty(CurNode, "static", TRUE);*/
	general_power_lib->mux_power->static_power = GetFloatProperty(CurNode,
			"static", TRUE, 0);
	FreeNode(CurNode);

	CurNode = FindElement(Node, "buf_power", TRUE);
	/*Prop = FindProperty(CurNode, "at_freq", TRUE);*/
	general_power_lib->buffer_power->at_freq = GetFloatProperty(CurNode,
			"at_freq", TRUE, 0);
	/*Prop = FindProperty(CurNode, "dynamic", TRUE);*/
	general_power_lib->buffer_power->dynamic_power = GetFloatProperty(CurNode,
			"dynamic", TRUE, 0);
	/*Prop = FindProperty(CurNode, "static", TRUE);*/
	general_power_lib->buffer_power->static_power = GetFloatProperty(CurNode,
			"static", TRUE, 0);
	FreeNode(CurNode);

	/*CurNode = FindElement(Node, "sb_switch_power", TRUE);
	 Prop = FindProperty(CurNode, "at_freq", TRUE);
	 general_power_lib->sb_switch_power->at_freq = GetFloatProperty(CurNode,
	 "at_freq", TRUE, 0);
	 Prop = FindProperty(CurNode, "dynamic", TRUE);
	 general_power_lib->sb_switch_power->dynamic_power = GetFloatProperty(
	 CurNode, "dynamic", TRUE, 0);
	 Prop = FindProperty(CurNode, "static", TRUE);
	 general_power_lib->sb_switch_power->static_power = GetFloatProperty(CurNode,
	 "static", TRUE, 0);
	 general_power_lib->sb_switch_power->num_conf_bits = GetIntProperty(CurNode,
	 "conf_bits_num", TRUE, 0);
	 FreeNode(CurNode);*/

	CurNode = FindElement(Node, "mem_power", TRUE);
	/*Prop = FindProperty(CurNode, "write_energy", TRUE);*/
	general_power_lib->mem_power->write_energy = GetFloatProperty(CurNode,
			"write_energy", TRUE, 0);
	/*Prop = FindProperty(CurNode, "static", TRUE);*/
	general_power_lib->mem_power->static_power = GetFloatProperty(CurNode,
			"static", TRUE, 0);
	FreeNode(CurNode);

}

void ProcessPbPowerLibrary(ezxml_t Node) {

	ezxml_t CurType, Prev;
	int i;

	t_power_instance *instance;

	num_root_power_instances = CountChildren(Node, "pb_power", 1);

	complex_block_instances = (t_power_instance *) my_malloc(
			num_root_power_instances * sizeof(t_power_instance));

	CurType = Node->child;
	i = 0;
	while (CurType) {
		instance = &(complex_block_instances[i]);

		ProcessPbPower(CurType, instance, NULL);

		instance->index = i++;

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

	general_power_lib = (t_general_power_lib *) my_malloc(
			sizeof(t_general_power_lib));

	if (Prop != NULL && (strcmp(Prop, "pass_gate_based_switches") == 0))
		general_power_lib->switch_type = PASS_GATE_BASED;
	else if (Prop == NULL || (strcmp(Prop, "mux_based_switches") == 0))
		general_power_lib->switch_type = MUX_BASED;
	else {
		printf("Undefined switch type for power library '%s'.\n", Prop);
		exit(1);
	}

	Next = FindElement(Cur, "pb_power_lib", TRUE);
	ProcessPbPowerLibrary(Next);
	FreeNode(Next);

	Next = FindElement(Cur, "general_power_lib", TRUE);
	ProcessGeneralPowerLibrary(Next);
	FreeNode(Next);

	FreeNode(Cur);

	printf("Power library '%s' successfully loaded!\n", power_library);
}

void power_complex_block(t_power_usage *complex_block_power,
		t_power_usage *mem_power) {
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

				for (i = 0; i < num_root_power_instances; ++i) {
					if (strcmp(complex_block_instances[i].name,
							grid[x][y].type->name) == 0) {
						power_pb(complex_block_power, mem_power, pb,
								grid[x][y].type->pb_graph_head,
								complex_block_instances[i]);
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

void power_pb(t_power_usage *complex_block_power, t_power_usage *mem_power,
		t_pb *pb, t_pb_graph_node *pb_node, t_power_instance instance) {

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

	if (instance.type == IGNORE)
		return;

	if (instance.type == MACRO) {
		/* This is a leaf node, which is a primitive (lut, ff, etc) */

		mem_power->leakage += general_power_lib->mem_power->static_power
				* instance.num_conf_bits;
		logic_conf_bits += instance.num_conf_bits;

		if (!pb) {
			complex_block_power->leakage += instance.static_power_avg;
			primitive_power += instance.static_power_avg;
			return;
		}

		average_sa = 1;
		pin_num = 0;
		for (port_idx = 0; port_idx < pb_node->num_input_ports; ++port_idx) {
			for (pin_idx = 0; pin_idx < pb_node->num_input_pins[port_idx];
					++pin_idx) {
				pin = &pb_node->input_pins[port_idx][pin_idx];
				average_sa *= 1 - pin_dens(pb, pin) / 2;
				pin_num++;
			}
		}
		/* average_sa is the probability of the circuit is silent
		 * 1 - average_sa is the probability that any input is switching*/
		average_sa = 1 - average_sa;

		complex_block_power->dynamic += average_sa * instance.dynamic_power_avg
				/ T_critical_path / instance.at_freq;
		complex_block_power->leakage += (1 - average_sa)
				* instance.static_power_avg;
		primitive_power += average_sa * instance.dynamic_power_avg
				/ T_critical_path / instance.at_freq
				+ (1 - average_sa) * instance.static_power_avg;

	} else {
		/*This node contains children*/
		assert(instance.type == SUM);
		if (pb)
			pb_mode = pb->mode;
		else {
			/*make it default mode*/
			pb_mode = 0;
		}
		for (pb_type_idx = 0;
				pb_type_idx
						< pb_node->pb_type->modes[pb_mode].num_pb_type_children;
				pb_type_idx++) {
			defined = FALSE;
			for (pb_idx = 0;
					pb_idx
							< pb_node->pb_type->modes[pb_mode].pb_type_children[pb_type_idx].num_pb;
					pb_idx++) {
				t_pb * child_pb = NULL;
				t_pb_graph_node * child_pb_graph_node;

				if (pb && pb->child_pbs[pb_type_idx][pb_idx].name) {
					/* Child is initialized */
					child_pb = &pb->child_pbs[pb_type_idx][pb_idx];
				}
				child_pb_graph_node =
						&pb_node->child_pb_graph_nodes[pb_mode][pb_type_idx][pb_idx];
				for (i = 0; i < instance.num_children_types; ++i) {
					if (strcmp(child_pb_graph_node->pb_type->name,
							instance.children[i].name) == 0) {
						defined = TRUE;
						power_pb(complex_block_power, mem_power, child_pb,
								child_pb_graph_node, instance.children[i]);
						break;
					}
				}

			}
			if (!defined) {
				printf(
						"Pb_type %s not defined in the library, power ignored.\n",
						pb_node->pb_type->modes[pb_mode].pb_type_children[pb_type_idx].name);
			}
		}

		for (interc_idx = 0;
				interc_idx < pb_type->modes[pb_mode].num_interconnect;
				interc_idx++) {
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

				for (in_port_idx = 0;
						in_port_idx < interconnect->num_input_pb_graph_node_sets;
						in_port_idx++) {
					for (pin_idx = 0;
							pin_idx
									< interconnect->num_input_pb_graph_node_pins[in_port_idx];
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

				for (out_port_idx = 0;
						out_port_idx
								< interconnect->num_output_pb_graph_node_sets;
						out_port_idx++) {
					for (pin_idx = 0;
							pin_idx
									< interconnect->num_output_pb_graph_node_pins[out_port_idx];
							pin_idx++) {
						if (!clock_flag) {
							t_pb_graph_pin * output_pin =
									interconnect->output_pb_graph_node_pins[out_port_idx][pin_idx];
							complex_block_power->dynamic +=
									num_default_muxes(num_inputs,
											default_mux_size)
											* general_power_lib->mux_power->dynamic_power
											/ T_critical_path
											/ general_power_lib->mux_power->at_freq
											* average_sa;
							complex_block_power->leakage += num_default_muxes(
									num_inputs, default_mux_size)
									* general_power_lib->mux_power->static_power
									* (1 - average_sa);

							num_muxes += num_default_muxes(num_inputs,
									default_mux_size);
							avg_sa += average_sa;

							inter_power +=
									num_default_muxes(num_inputs,
											default_mux_size)
											* general_power_lib->mux_power->dynamic_power
											/ T_critical_path
											/ general_power_lib->mux_power->at_freq
											* average_sa
											+ num_default_muxes(num_inputs,
													default_mux_size)
													* general_power_lib->mux_power->static_power
													* (1 - average_sa);

							mem_power->leakage +=
									general_power_lib->mem_power->static_power
											* num_default_muxes(num_inputs,
													default_mux_size)
											* ceil(
													log(default_mux_size)
															/ log(2));
							logic_conf_bits += num_default_muxes(num_inputs,
									default_mux_size)
									* ceil(log(default_mux_size) / log(2));
						}
					}
				}

				break;
			}
		}

	}

}

void power_routing(t_power_usage *routing_power, t_power_usage *mem_power) {
	int i, rr_node_idx, net_idx, num_sb;
	t_rr_node *node, *prev, *next;
	struct s_trace *trace;
	int wire_length;
	int fan_in, edge;
	float C_wire;
	float fanin_sa;
	const t_segment_inf *seg_details;
	int *chan_driven_by_chan = (int *) my_malloc(num_rr_nodes * sizeof(int));
	float *average_fan_in_sa = (float *) my_malloc(
			num_rr_nodes * sizeof(float));
	float temp_dyn, temp_leakage;

	int total_conf_bits, cb_fanout, sb_fanout;

	seg_details = Arch.Segments;

	t_net_activity *clb_net_activity = (t_net_activity *) my_malloc(
			sizeof(t_net_activity) * num_nets);

	for (i = 0; i < num_nets; ++i) {
		clb_net_activity[i].probability =
				blif_net_activity[clb_to_vpack_net_mapping[i]].probability;
		clb_net_activity[i].density =
				blif_net_activity[clb_to_vpack_net_mapping[i]].density;
	}

	for (i = 0; i < num_rr_nodes; ++i) {
		chan_driven_by_chan[i] = 0;
		average_fan_in_sa[i] = 1.;
		rr_node[i].net_num = OPEN;
	}

	for (net_idx = 0; net_idx < num_nets; net_idx++) {
		for (trace = trace_head[net_idx]; trace != NULL; trace = trace->next) {
			prev = &rr_node[trace->index];
			prev->net_num = net_idx;
			if (trace->next == NULL)
				break;
			node = &rr_node[trace->next->index];
			node->net_num = net_idx;
			if ((prev->type == CHANX || prev->type == CHANY)
					&& (node->type == CHANX || node->type == CHANY))
				chan_driven_by_chan[trace->next->index] = 1;
		}
	}

	for (rr_node_idx = 0; rr_node_idx < num_rr_nodes; rr_node_idx++) {
		if (rr_node[rr_node_idx].net_num != OPEN) {
			net_idx = rr_node[rr_node_idx].net_num;
			for (edge = 0; edge < rr_node[rr_node_idx].num_edges; ++edge) {
				average_fan_in_sa[rr_node[rr_node_idx].edges[edge]] *= 1
						- clb_net_activity[net_idx].density / 2.0;
			}
		}
	}

	float wire_power = 0, sb_power = 0, cb_power = 0;

	total_conf_bits = 0;
	temp_dyn = temp_leakage = 0;
	for (rr_node_idx = 0; rr_node_idx < num_rr_nodes; rr_node_idx++) {
		node = &rr_node[rr_node_idx];
		fanin_sa = 1 - average_fan_in_sa[rr_node_idx];
		cb_fanout = sb_fanout = 0;
		fan_in = -1;
		if (general_power_lib->switch_type == MUX_BASED) {
			int default_mux_size = general_power_lib->mux_power->mux_size;
			switch (node->type) {
			case SOURCE:
			case SINK:
			case OPIN:
				break;
			case IPIN:
				num_ipin++;
				fan_in = node->fan_in;
				if (fan_in) {
					avg_ipin_fanin += fan_in;
					routing_power->dynamic += fanin_sa
							* general_power_lib->mux_power->dynamic_power
							* num_default_muxes(fan_in, default_mux_size)
							/ general_power_lib->mux_power->at_freq
							/ T_critical_path;
					routing_power->leakage += (1 - fanin_sa)
							* general_power_lib->mux_power->static_power
							* num_default_muxes(fan_in, default_mux_size);

					cb_num_muxes += num_default_muxes(fan_in,
														default_mux_size);

					/*cb_power += fanin_sa
					 * general_power_lib->mux_power->dynamic_power
					 * num_default_muxes(fan_in, default_mux_size)
					 / general_power_lib->mux_power->at_freq
					 / T_critical_path
					 + (1 - fanin_sa)
					 * general_power_lib->mux_power->static_power
					 * num_default_muxes(fan_in,
					 default_mux_size);*/
					mem_power->leakage +=
							general_power_lib->mem_power->static_power
									* num_default_muxes(fan_in,
											default_mux_size)
									* ceil(log(default_mux_size) / log(2));
					total_conf_bits += num_default_muxes(fan_in,
							default_mux_size)
							* ceil(log(default_mux_size) / log(2));
				}
				break;
			case CHANX:
			case CHANY:
				num_chan++;

				fan_in = node->fan_in;
				avg_chan_fanin+=fan_in;
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
				routing_power->dynamic += general_power_lib->Vdd
						* general_power_lib->Vdd * C_wire / T_critical_path
						* clb_net_activity[node->net_num].density / 2;
				wire_power += general_power_lib->Vdd * general_power_lib->Vdd
						* C_wire / T_critical_path
						* clb_net_activity[node->net_num].density / 2;

				/* SB Power that this wire drives */
				/*routing_power->dynamic += num_sb
				 * general_power_lib->sb_switch_power->dynamic_power
				 / general_power_lib->sb_switch_power->at_freq
				 / T_critical_path * clb_net_activity[node->net_num].density / 2;
				 routing_power->leakage += num_sb
				 * general_power_lib->sb_switch_power->static_power
				 * (1 - clb_net_activity[node->net_num].density / 2);*/

				/* Buffer to SB */
				routing_power->dynamic +=
						general_power_lib->buffer_power->dynamic_power
								/ T_critical_path
								/ general_power_lib->buffer_power->at_freq
								* clb_net_activity[node->net_num].density / 2;
				routing_power->leakage +=
						general_power_lib->buffer_power->static_power
								* (1
										- clb_net_activity[node->net_num].density
												/ 2);
				num_buffers+=2;
				/*sb_power +=
				 general_power_lib->buffer_power->dynamic_power
				 / T_critical_path
				 / general_power_lib->buffer_power->at_freq
				 * clb_net_activity[node->net_num].density / 2
				 + general_power_lib->buffer_power->static_power
				 * (1
				 - clb_net_activity[node->net_num].density
				 / 2);*/

				routing_power->dynamic +=
						general_power_lib->buffer_power->dynamic_power
								/ T_critical_path
								/ general_power_lib->buffer_power->at_freq
								* clb_net_activity[node->net_num].density / 2;
				routing_power->leakage +=
						general_power_lib->buffer_power->static_power
								* (1
										- clb_net_activity[node->net_num].density
												/ 2);

				/*cb_power +=
				 general_power_lib->buffer_power->dynamic_power
				 / T_critical_path
				 / general_power_lib->buffer_power->at_freq
				 * clb_net_activity[node->net_num].density / 2
				 + general_power_lib->buffer_power->static_power
				 * (1
				 - clb_net_activity[node->net_num].density
				 / 2);*/

				/* SB Power that drives the wire */
				routing_power->dynamic += fanin_sa
						* general_power_lib->mux_power->dynamic_power
						* num_default_muxes(fan_in, default_mux_size)
						/ general_power_lib->mux_power->at_freq
						/ T_critical_path;
				routing_power->leakage += (1 - fanin_sa)
						* general_power_lib->mux_power->static_power
						* num_default_muxes(fan_in, default_mux_size);

				/*sb_power += fanin_sa
				 * general_power_lib->mux_power->dynamic_power
				 * num_default_muxes(fan_in, default_mux_size)
				 / general_power_lib->mux_power->at_freq
				 / T_critical_path
				 + (1 - fanin_sa)
				 * general_power_lib->mux_power->static_power
				 * num_default_muxes(fan_in, default_mux_size);*/

				sb_num_muxes += num_default_muxes(fan_in,
													default_mux_size);

				/* Mem Power */
				mem_power->leakage += general_power_lib->mem_power->static_power
						* num_default_muxes(fan_in, default_mux_size)
						* ceil(log(default_mux_size) / log(2));
				/*mem_power->leakage += general_power_lib->mem_power->static_power
				 * num_default_muxes(fan_in, default_mux_size)
				 * general_power_lib->sb_switch_power->num_conf_bits
				 * num_sb;*/
				total_conf_bits += num_default_muxes(fan_in, default_mux_size)
						* ceil(log(default_mux_size) / log(2));

				/*if (node->net_num != OPEN) {
				 The wire is active
				 routing_power->dynamic += general_power_lib->Vdd
				 * general_power_lib->Vdd * C_wire / T_critical_path
				 * sa;
				 if (chan_driven_by_chan[rr_node_idx]) {
				 The channel is driven by a switch box
				 routing_power->dynamic +=
				 general_power_lib->sb_switch_power->dynamic_power
				 / general_power_lib->sb_switch_power->at_freq
				 / T_critical_path * sa;
				 routing_power->leakage +=
				 general_power_lib->sb_switch_power->static_power
				 * (1 - sa);
				 All the CB MUXs that are not used to drive this channel.
				 * Note that for each channel there will be a determined CB MUX to drive it
				 routing_power->leakage +=
				 general_power_lib->cb_switch_power->static_power
				 * num_cb;
				 routing_power->leakage +=
				 general_power_lib->sb_switch_power->static_power
				 * (num_sb - 1);
				 To remove one of the static sb power assumed by the previous driving channel

				 } else {
				 The channel is driven by an output pin.
				 * The output connection box has a fan-in of
				 routing_power->dynamic +=
				 general_power_lib->cb_switch_power->dynamic_power
				 / general_power_lib->cb_switch_power->at_freq
				 / T_critical_path * sa;
				 routing_power->leakage +=
				 general_power_lib->cb_switch_power->static_power
				 * (1 - sa);
				 All the CB MUXs that are not used to drive this channel.
				 * Note that for each channel there will be a determined CB MUX to drive it
				 routing_power->leakage +=
				 general_power_lib->cb_switch_power->static_power
				 * (num_cb - 1);
				 All the switching boxes that this channel drives will consume static power for
				 * now until any of them found to be active.
				 routing_power->leakage +=
				 general_power_lib->sb_switch_power->static_power
				 * num_sb;
				 }

				 } else {
				 All the CB MUXs that are not used to drive this channel.
				 * Note that for each channel there will be a determined CB MUX to drive it
				 routing_power->leakage +=
				 general_power_lib->cb_switch_power->static_power
				 * num_cb;
				 All the switching boxes that this channel drives will consume static power for
				 * now until any of them found to be active.
				 routing_power->leakage +=
				 general_power_lib->sb_switch_power->static_power
				 * num_sb;
				 }
				 Connection Box Mem
				 mem_power->leakage += general_power_lib->mem_power->static_power
				 * ceil(log(node->fan_in) / log(2));
				 total_conf_bits += ceil(log(node->fan_in) / log(2));
				 Switching Box Mem
				 mem_power->leakage += general_power_lib->mem_power->static_power
				 * general_power_lib->sb_switch_power->num_conf_bits
				 * num_sb;
				 total_conf_bits +=
				 general_power_lib->sb_switch_power->num_conf_bits
				 * num_sb;*/
				break;
			default:
				break;
			}
			/*printf(
			 "power usage of rr_node #%d, dynamic: %.2e, leakage: %.2e, fanin_sa: %.5f, fanin: %d\n",
			 rr_node_idx, routing_power->dynamic - temp_dyn,
			 routing_power->leakage - temp_leakage, fanin_sa, fan_in);
			*/temp_dyn = routing_power->dynamic;
			temp_leakage = routing_power->leakage;
		}
	}

	free(chan_driven_by_chan);
	free(average_fan_in_sa);
	printf("Total # of configuration bits in routing: %d\n", total_conf_bits);
	printf("Total # of cb_muxes %d, Total # of sb_muxes %d, Total # of buffers %d\n", cb_num_muxes, sb_num_muxes, num_buffers);
	printf("Total wire power: %.2e\n", wire_power);
}

void power_zero_usage(t_power_usage *power_usage) {
	power_usage->dynamic = 0;
	power_usage->leakage = 0;
}

static int num_default_muxes(int input_size, int default_mux_size) {
	int i;

	i = 1;
	while (input_size > default_mux_size) {
		input_size = ceil(1.0 * input_size / default_mux_size);
		i += input_size;

	}
	return i;
}


