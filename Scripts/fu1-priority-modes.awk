# ---------------------------------------------------------------------------
#  Phase 6 / FU-1: joins the v4 mode table to the local modulation response and
#  writes fu1-priority-modes.csv.
#
#  Reporting only. Nothing here re-measures anything, nothing is re-tracked and
#  no threshold used by the tracker or the classifier is touched. Every column
#  is either copied from one of the two inputs or is arithmetic on them, and
#  the one derived number - the priority score - is defined in the report and
#  is an ordering, not a verdict.
#
#  Usage:
#    awk -f Scripts/fu1-priority-modes.awk \
#        Phase6ModalMap-v4/local-response.csv Phase6ModalMap-v4/mode-table.csv \
#        > Phase6ModalMap-v4/fu1-priority-modes.csv
#
#  local-response.csv must come first: it is read into memory and then joined
#  onto the mode table row by row on mode_id.
# ---------------------------------------------------------------------------

BEGIN {
    FPAT = "([^,]*)|(\"[^\"]*\")"
    OFS = ","
}

function unquote (s) { gsub(/^"|"$/, "", s); return s }

# --- first file: the local modulation response -----------------------------

FNR == 1 && NR == 1 {
    for (i = 1; i <= NF; i++) lr[$i] = i
    next
}

NR == FNR {
    id = $lr["mode_id"] + 0

    lrMeasured[id]      = $lr["measured"] + 0
    lrEnergyChange[id]  = $lr["energy_change_db"]
    lrMaxProm0[id]      = $lr["max_prom_depth0_db"]
    lrMaxPromD[id]      = $lr["max_prom_default_db"]
    lrSpread0[id]       = $lr["spread_depth0_hz"]
    lrSpreadD[id]       = $lr["spread_default_hz"]
    lrSpreadRatio[id]   = $lr["spread_ratio"]
    lrConc0[id]         = $lr["concentration_depth0"]
    lrConcD[id]         = $lr["concentration_default"]
    lrConcRatio[id]     = $lr["concentration_ratio"]
    lrPeaks0[id]        = $lr["peaks_depth0"] + 0
    lrPeaksD[id]        = $lr["peaks_default"] + 0
    next
}

# --- second file: the mode table -------------------------------------------

FNR == 1 {
    for (i = 1; i <= NF; i++) mt[$i] = i

    print "mode_id,frequency_hz,class,class_letter,prominence_db,bandwidth_hz,q," \
          "width_resolved,size_matched_points,size_ambiguous_points,size_gap_points," \
          "size_boundary_exit_points,size_q_cv,size_fit_r_squared," \
          "size_fit_exponent,size_residual_rms_hz,size_mean_residual_spacings," \
          "size_fb_same,size_fb_comparable,size_anchor_up_same,size_anchor_up_comparable," \
          "size_anchor_down_same,size_anchor_down_comparable,size_identity_label," \
          "size_evidence,mod_measured,mod_energy_change_db,mod_max_prom_change_db," \
          "mod_spread_ratio,mod_concentration_ratio,mod_peaks_depth0,mod_peaks_default," \
          "mod_peak_count_change,mod_excursion_hz,stereo_measured,band_correlation," \
          "lr_imbalance_db,decay_status,priority_score"
    next
}

{
    id = $mt["mode_id"] + 0
    cls = unquote($mt["class"])

    letter = "?"
    if (cls ~ /^A:/) letter = "A"
    else if (cls ~ /^B:/) letter = "B"
    else if (cls ~ /^C:/) letter = "C"
    else if (cls ~ /^D:/) letter = "D"

    promChange = ""
    if (lrMeasured[id] == 1 && lrMaxProm0[id] != "" && lrMaxPromD[id] != "")
        promChange = sprintf("%.4f", lrMaxPromD[id] - lrMaxProm0[id])

    peakChange = ""
    if (lrMeasured[id] == 1)
        peakChange = lrPeaksD[id] - lrPeaks0[id]

    imbalance = ""
    if ($mt["stereo_measured"] + 0 == 1)
        imbalance = sprintf("%.4f", $mt["energy_left_db"] - $mt["energy_right_db"])

    # --- priority score -----------------------------------------------------
    #
    # An ordering for a listening session, not a judgement. It rises with the
    # three things asked for - how far the peak stands above its neighbourhood,
    # how concentrated the neighbourhood's energy is, and how little the
    # modulation spreads it - and is only defined where the local response was
    # measured. It says nothing about whether a mode is audible or unwanted.
    score = ""

    if (lrMeasured[id] == 1 && lrConcD[id] != "" && lrSpreadRatio[id] != "") {
        prom = $mt["prominence_db"] + 0
        conc = lrConcD[id] + 0
        sr   = lrSpreadRatio[id] + 0

        resist = 1.0 / (sr > 0 ? sr : 1.0)

        score = sprintf("%.4f", (prom / 10.0) * conc * resist)
    }

    print id, $mt["frequency_hz"], "\"" cls "\"", letter,
          $mt["prominence_db"], $mt["bandwidth_hz"], $mt["q"], $mt["width_resolved"],
          $mt["size_matched_points"], $mt["size_ambiguous_points"],
          $mt["size_gap_points"], $mt["size_boundary_exit_points"],
          $mt["size_q_cv"], $mt["size_fit_r_squared"],
          $mt["size_fit_exponent"], $mt["size_residual_rms_hz"],
          $mt["size_mean_residual_spacings"],
          $mt["size_fb_same"], $mt["size_fb_comparable"],
          $mt["size_anchor_up_same"], $mt["size_anchor_up_comparable"],
          $mt["size_anchor_down_same"], $mt["size_anchor_down_comparable"],
          $mt["size_identity_label"], $mt["size_evidence"],
          lrMeasured[id], lrEnergyChange[id], promChange,
          lrSpreadRatio[id], lrConcRatio[id], lrPeaks0[id], lrPeaksD[id], peakChange,
          $mt["mod_excursion_hz"], $mt["stereo_measured"], $mt["band_correlation"],
          imbalance, unquote($mt["decay_status"]), score
}
