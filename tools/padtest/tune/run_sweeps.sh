#!/bin/sh
# run_sweeps.sh -- every measurement this lane rests on, in the order it is
# quoted. Each line writes its raw trace into evidence/ so the claim can be
# re-made rather than believed.
#
#   ./run_sweeps.sh            all of it (~5 min)
set -u
cd "$(dirname "$0")"
OLD=${OLD:-$(pwd)/out-ref/cmr2}   # the reference build; see the README
NEW=${NEW:-$(pwd)/out/cmr2}
S=1.0
EV=evidence
STICK="0,600,700,900,1000,2000,4000,6000,8000,9000,9800,10000,12000,16000,20000,24000,28000,32767"
TRIG="0,3,5,6,10,20,40,60,76,77,90,128,200,255"

run() {   # run <outfile> <tag> <bin> <axis> <vals> [env...]
  out=$1; tag=$2; bin=$3; axis=$4; vals=$5; shift 5
  echo "=== $tag"
  set -- "$@"   # remaining are K=V
  envargs=""
  for kv in "$@"; do envargs="$envargs --env $kv"; done
  # shellcheck disable=SC2086
  python3 sweep.py --bin "$bin" --tag "$tag" --axis "$axis" --vals "$vals" \
      --settle $S --raw "$EV/$out.raw" $envargs | tee "$EV/$out.txt"
}

run old-ez        "A  REFERENCE: pre-padtune behaviour + the same trace"                     "$OLD" ABS_X "$STICK"
run new-ez        "B  NEW binary, DECK_PAD_TIER=ez (the default)"  "$NEW" ABS_X "$STICK" DECK_PAD_TIER=ez
run new-ez-knobs  "C  NEW binary, EZ *with every knob set wild*"   "$NEW" ABS_X "$STICK" \
    DECK_PAD_TIER=ez DECK_PAD_STEER_DEADZONE=4000 DECK_PAD_STEER_CURVE=180 \
    DECK_PAD_STEER_SAT=3000 DECK_PAD_TRIGGER_L_DEADZONE=5000 DECK_PAD_AXIS0_CURVE=30
run adv-dz3000    "D  ADVANCED, STEER_DEADZONE=3000 (30%)"         "$NEW" ABS_X "$STICK" \
    DECK_PAD_TIER=advanced DECK_PAD_STEER_DEADZONE=3000
run adv-curve180  "E  ADVANCED, STEER_CURVE=180 (soft near centre)" "$NEW" ABS_X "$STICK" \
    DECK_PAD_TIER=advanced DECK_PAD_STEER_CURVE=180
run adv-curve30   "F  ADVANCED, STEER_CURVE=30 (sharp)"            "$NEW" ABS_X "$STICK" \
    DECK_PAD_TIER=advanced DECK_PAD_STEER_CURVE=30
run adv-sat5000   "G  ADVANCED, STEER_SAT=5000 (full lock at half travel)" "$NEW" ABS_X "$STICK" \
    DECK_PAD_TIER=advanced DECK_PAD_STEER_SAT=5000
run trig-ez       "H  injection baseline: the left trigger, nothing set" "$NEW" ABS_Z "$TRIG"
run trig-l-dz3000 "I  ADVANCED, TRIGGER_L_DEADZONE=3000 -- the trigger being swept" "$NEW" ABS_Z "$TRIG" \
    DECK_PAD_TIER=advanced DECK_PAD_TRIGGER_L_DEADZONE=3000
run trig-r-dz3000 "J  ADVANCED, TRIGGER_R_DEADZONE=3000 -- control: must not move L" "$NEW" ABS_Z "$TRIG" \
    DECK_PAD_TIER=advanced DECK_PAD_TRIGGER_R_DEADZONE=3000
echo "=== done; tables in $EV/"
