# POST-EYE candidate: drive195deg after actual0/15/30deg carry-code errors.
# Effective period-normalized return phase hypothesis10..20ns, NOT measured
# absolute ADCtOD min/max, NOT a datasheet guarantee or PVT qualification.
# Datasheet25ns remains TYPICAL; unknown whole-cycle latency is not calibrated.
# Broad90/180/270deg diagnostic records are clean. Candidate195 is rebuilt and
# tested with neighboring180/210deg; oldfailed20..30nsmodel evidence retained.
# drive195/capture0: setup29.167ns and hold9.167ns requirements.
create_generated_clock -name adc_clock1 -source [get_pins -hier -filter {NAME =~ */adc_front/inst/clk1/C}] -divide_by 1 [get_ports ad_clk_1]
create_generated_clock -name adc_clock2 -source [get_pins -hier -filter {NAME =~ */adc_front/inst/clk2/C}] -divide_by 1 [get_ports ad_clk_2]
set_input_delay -clock adc_clock1 -max 20.0 [get_ports {ad_data_1[*] ad_otr_1}]
set_input_delay -clock adc_clock1 -min 10.0 [get_ports {ad_data_1[*] ad_otr_1}]
set_input_delay -clock adc_clock2 -max 20.0 [get_ports {ad_data_2[*] ad_otr_2}]
set_input_delay -clock adc_clock2 -min 10.0 [get_ports {ad_data_2[*] ad_otr_2}]
# ADC output latency crosses a conversion period. Captured word sequence and
# aperture boundaries are independently checked by tb_adc_frontend_25m.
set_multicycle_path 2 -setup -from [get_clocks {adc_clock1 adc_clock2}] -to [get_clocks raw]
# Explicit zero hold shift: raw20-adc10.833=9.167ns, no spare-cycle relaxation.
set_multicycle_path 0 -hold -end -from [get_clocks {adc_clock1 adc_clock2}] -to [get_clocks raw]
# Explicit inter-clock roundtrip margin, including both setup and hold.
set_clock_uncertainty -from [get_clocks {adc_clock1 adc_clock2}] -to [get_clocks raw] 1.0
set_property DRIVE 4 [get_ports {ad_clk_1 ad_clk_2 ad_oe_1 ad_oe_2}]
set_property SLEW SLOW [get_ports {ad_clk_1 ad_clk_2 ad_oe_1 ad_oe_2}]
set_false_path -to [get_ports {ad_oe_1 ad_oe_2}]
