source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def fence01.def
set_debug_level DPL "detailed" 4
detailed_placement
check_placement

set def_file [make_result_file fence01.def]
write_def $def_file
diff_file $def_file fence01.defok
