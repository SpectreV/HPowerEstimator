void read_blif (char *blif_file, boolean sweep_hanging_nets_and_inputs,
				t_model *user_models, t_model *library_models);
void echo_input (char *blif_file, char *echo_file, t_model *library_models); 

/*
static void read_activity(char * activity_file);

boolean add_activity_to_net(char * net_name, float probability, float density);
*/
