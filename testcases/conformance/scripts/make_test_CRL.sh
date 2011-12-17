#!/bin/bash
#

# make_test_CRL.sh - manually create CertificateRevocationList (CRL)
#       for RPKI syntax conformance test

# Set up RPKI environment variables if not already done.
THIS_SCRIPT_DIR=$(dirname $0)
. $THIS_SCRIPT_DIR/../../../envir.setup

# Safe bash shell scripting practices
set -o errexit			# exit if anything fails
set -o errtrace			# shell functions inherit 'ERR' trap
trap "echo Error encountered during execution of $0 1>&2" ERR

# Usage
usage ( ) {
    usagestr="
Usage: $0 [options] <serial> <subjectname>

Options:
  -P        \tApply patches instead of prompting user to edit (default = false)
  -o outdir \tOutput directory (default = ...conformance/raw/root)
  -p patchdir\tDirectory for saving/getting patches (default = ...conformance/raw/patches
  -h        \tDisplay this help file

This script creates a CRL, prompts the user multiple times to edit it
interactively (e.g., in order to introduce errors), and captures
those edits in '.patch' files (output of diff -u).  Later,
make_test_CRL.sh can replay the creation process by automatically
applying those patch files instead of prompting for user intervention.

This tool takes as input a parent CA certificate + key pair, and as
output, issues a child CA certificate with a minimal publication
subdirectory.  The diagram below shows outputs of the script.  The
inputs and non-participants are indicated by normal boxes; the outputs
are indicated by boxes whose label has a prepended asterisk (*).
Note: this script does NOT update the 'Manifest issued by Parent'.

    <Need a box around root, showing the top level directory>

                    +--------+
         +--------->|  Root  |
         |          |  AIA   |
         |  +--------- SIA   |
         |  |       +--------+
         |  |
         |  |  +----------------------------------------+
         |  |  | rsync://rpki.bbn.com/conformance/root/	|
         |  +->|   +--------+     +------------+       	|
         |     |   | *Child |     | CRL issued |        |
         |     |   | CRLDP------->| by Parent  |        |
         +----------- AIA   |     +------------+        |
               |   |  SIA------+                        |
               |   +--------+  |  +-----------------+   |
               |               |  | Manifest issued |   |
               | Root's Repo   |  | by Parent       |   |
               | Directory     |  +-----------------+   |
               +---------------|------------------------+
                               |
                               V
	     +-------------------------------------------------+
       	     | rsync://rpki.bbn.com/conformance/root/subjname/ |
             |                                     	       |
             |     +---------------------------+   	       |
             |     | *Manifest issued by Child |               |
             |     +---------------------------+               |
             |                                                 |
             |     +----------------------------------+        |
             |     | *CRL issued by Child (TEST CASE) |        |
             |     +----------------------------------+        |
             |                                                 |
             | *Child's Repo Directory                         |
             +-------------------------------------------------+

Inputs:
  subjectname - subject name for the child
  serial - serial number for the child to be created
  -P - (optional) use patch mode for automatic insertion of patches
  patchdir - (optional) local path to directory of patches
  outdir - (optional) local path to parent's repo directory.  Defaults to CWD

Outputs:
  child CA certificate - inherits AS/IP resources from parent via inherit bit
  path files - manual edits are saved as diff output in
              'badCRL<subjectname>.stageN.patch' (N=0..1)

  child repo directory - ASSUMED to be a subdirectory of parent's repo. The
                         new directory will be <outdir>/<subjectname>/
  crl issued by child - named <subjectname>.crl, and has no entries
  mft issued by child - named <subjectname>.mft, and has one entry (the crl)

  The filename for the crl will be prepended by the string 'bad'.

Auxiliary Outputs: (not shown in diagram)
  child key pair - <outdir>/<subjectname>.p15
  child-issued MFT EE cert - <outdir>/<subjectname>/<subjectname>.mft.cer
  child-issued MFT EE key pair - <outdir>/<subjectname>/<subjectname>.mft.p15
    "
    printf "${usagestr}\n"
    exit 1
}

# NOTES

# 1. Variable naming convention -- preset constants and command line
# arguments are in ALL_CAPS.  Derived/computed values are in
# lower_case.

# 2. Assumes write-access to current directory even though the output
# directory will be different.

# Set up paths to ASN.1 tools.
CGTOOLS=$RPKI_ROOT/cg/tools	# Charlie Gardiner's tools

# Options and defaults
OUTPUT_DIR="$RPKI_ROOT/testcases/conformance/raw/root"
PATCHES_DIR="$RPKI_ROOT/testcases/conformance/raw/patches"
ROOT_KEY_PATH="$RPKI_ROOT/testcases/conformance/raw/root.p15"
ROOT_CERT_PATH="$RPKI_ROOT/testcases/conformance/raw/root.cer"
USE_EXISTING_PATCHES=
EDITOR=${EDITOR:-vi}            # set editor to vi if undefined
# Process command line arguments.
while getopts Pk:o:p:h opt
do
  case $opt in
      P)
	  USE_EXISTING_PATCHES=1
	  ;;
      k)
	  ROOT_KEY_PATH=$OPTARG
	  ;;
      o)
	  OUTPUT_DIR=$OPTARG
	  ;;
      p)
          PATCHES_DIR=$OPTARG
          ;;
      h)
	  usage
	  ;;
  esac
done
shift $((OPTIND - 1))
if [ $# = "2" ]
then
    SERIAL=$1
    SUBJECTNAME=$2
else
    usage
fi

###############################################################################
# Computed Variables
###############################################################################

child_name="${SUBJECTNAME}"
crl_name="bad${child_name}"


###############################################################################
# Check for prerequisite tools and files
###############################################################################

ensure_file_exists ( ) {
    if [ ! -e "$1" ]
    then
	echo "Error: file not found - $1" 1>&2
	exit 1
    fi
}

ensure_dir_exists ( ) {
    if [ ! -d "$1" ]
    then
        echo "Error: directory not found - $1" 1>&2
        exit 1
    fi
}

ensure_dir_exists $OUTPUT_DIR
ensure_dir_exists $PATCHES_DIR

ensure_file_exists $ROOT_KEY_PATH
ensure_file_exists $ROOT_CERT_PATH
ensure_file_exists $CGTOOLS/rr
ensure_file_exists $CGTOOLS/dump
ensure_file_exists $CGTOOLS/dump_smart
ensure_file_exists $CGTOOLS/sign_cert
ensure_file_exists $CGTOOLS/fix_manifest

if [ $USE_EXISTING_PATCHES ]
then
    ensure_file_exists $PATCHES_DIR/${crl_name}.stage0.patch
    ensure_file_exists $PATCHES_DIR/${crl_name}.stage1.patch
fi

###############################################################################
# Generate Child cert
###############################################################################

# Create a good CRL in a subdirectory (but named bad<subjname>.crl)
$RPKI_ROOT/testcases/conformance/scripts/gen_child_ca.sh \
    -b crl \
    -o ${OUTPUT_DIR} \
    ${child_name} \
    ${SERIAL} \
    ${ROOT_CERT_PATH} \
    rsync://rpki.bbn.com/conformance/root.cer \
    ${ROOT_KEY_PATH} \
    rsync://rpki.bbn.com/conformance/root/root.crl

# Go into that subdirectory...
cd ${OUTPUT_DIR}/${child_name}
${CGTOOLS}/dump_smart ${crl_name}.crl >${crl_name}.raw

# Pre-signing modification: manual or automatic (can be no-op)
if [ $USE_EXISTING_PATCHES ]
then
    patch ${crl_name}.raw ${PATCHES_DIR}/${crl_name}.stage0.patch
else
    cp ${crl_name}.raw ${crl_name}.raw.old
    ${EDITOR} ${crl_name}.raw
    diff -u ${crl_name}.raw.old ${crl_name}.raw \
	>${PATCHES_DIR}/${crl_name}.stage0.patch || true
fi

# Sign it
${CGTOOLS}/rr <${crl_name}.raw >${crl_name}.blb
${CGTOOLS}/sign_cert ${crl_name}.blb ../${child_name}.p15
mv ${crl_name}.blb ${crl_name}.crl
${CGTOOLS}/dump -a ${crl_name}.crl >${crl_name}.raw

# Post-signing modification: manual or automatic (can be no-op)
if [ $USE_EXISTING_PATCHES ]
then
    patch ${crl_name}.raw ${PATCHES_DIR}/${crl_name}.stage1.patch
else
    cp ${crl_name}.raw ${crl_name}.raw.old
    ${EDITOR} ${crl_name}.raw
    diff -u ${crl_name}.raw.old ${crl_name}.raw \
	>${PATCHES_DIR}/${crl_name}.stage1.patch || true
fi

# Convert back into DER-encoded binary
${CGTOOLS}/rr <${crl_name}.raw >${crl_name}.crl

# Update manifest with hash of newly edited CRL
${CGTOOLS}/fix_manifest ${child_name}.mft \
    ${child_name}.mft.p15 ${crl_name}.crl

# Clean-up
rm ${crl_name}.raw
if [ ! $USE_EXISTING_PATCHES ]
then
    rm ${crl_name}.raw.old
fi

# Notify user of output locations
echo Successfully created "${OUTPUT_DIR}/${child_name}/${crl_name}.crl" and \
    auxiliary files.
if [ ! $USE_EXISTING_PATCHES ]
then
    echo Successfully created "${PATCHES_DIR}/${crl_name}.stage0.patch"
    echo Successfully created "${PATCHES_DIR}/${crl_name}.stage1.patch"
fi
