# ibex (low utilization)
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def ibex_core_replace.def
set_debug_level DPL detailed 1 
detailed_placement
check_placement

set def_file [make_result_file ibex.def]
write_def $def_file
diff_file $def_file ibex.defok
