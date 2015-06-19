/*
 * input.h
 *
 *  Created on: 2 Apr, 2014
 *      Author: liang
 */

#ifndef INPUT_H_
#define INPUT_H_

void build_arch(char * ArchFile, int TimingEnabled, t_arch *Arch);

void build_place(const char *place_file, const char *arch_file,
		const char *net_file, int num_blocks, struct s_block block_list[]);

void build_blif(char *blif_file, boolean sweep_hanging_nets_and_inputs,
		t_model *user_models, t_model *library_models, char *activity_file);

#endif /* INPUT_H_ */
