source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def fragmented_row01.def
set_debug_level DPL detailed 1 
detailed_placement
check_placement

set def_file [make_result_file fragmented_row01.def]
write_def $def_file
diff_file $def_file fragmented_row01.defok
