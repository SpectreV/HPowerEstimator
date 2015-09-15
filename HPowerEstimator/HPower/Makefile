CC = gcc
AR = ar
LIBFLAGS = rcs
CFLAGS = -Wall -pedantic -g -O0 

LIB_DIR = -L../libvpr -L.
LIB = -lm -lvpr_6 -lvpr_util -lX11

VPR_SRC_DIR = ../vpr/SRC
INC_FLAGS = -I../libvpr/include \
			-I$(VPR_SRC_DIR)/util \
			-I$(VPR_SRC_DIR)/timing \
			-I$(VPR_SRC_DIR)/pack \
			-I$(VPR_SRC_DIR)/place \
			-I$(VPR_SRC_DIR)/base \
			-I$(VPR_SRC_DIR)/route
			
VPR_OBJS = ../vpr/OBJ/*/*.o

VPR_LIB = libvpr_util.a


			
EXE = hpower

OBJS = input.o power.o blif.o route.o main.o 

%.o: %.c
	$(CC) $(CFLAGS) $(INC_FLAGS) -c $< -o $@  
	

$(EXE): $(OBJS) Makefile ../libvpr/libvpr_6.a $(VPR_LIB)
	$(CC) $(CFLAGS) $(INC_FLAGS) -o $(EXE) $(OBJS) $(LIB_DIR) $(LIB) 
	
$(VPR_LIB): 
	$(AR) $(LIBFLAGS) $@ $(VPR_OBJS)
	
	
	
clean:
	rm -f *.o *.a
	rm -f $(EXE) 
