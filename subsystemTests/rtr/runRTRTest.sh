#!/bin/sh -e

cd "`dirname "$0"`"

. ../../envir.setup


SERVER="$RPKI_ROOT/rtr/rtrd"
CLIENT="$RPKI_ROOT/rtr/rtr-test-client"
PORT=1234
NONCE=42
WRONG_NONCE=4242

client_raw () {
	SUBTEST_NAME="$1"
	shift
	EXPECTED_RESULTS="$1"
	shift
	echo "--- $SUBTEST_NAME" | tee -a response.log
	echo "--- expecting: $EXPECTED_RESULTS" | tee -a response.log
	"$@" | "$CLIENT" client_one localhost $PORT | tee -a response.log
}

client () {
	COMMAND="$1"
	EXPECTED_RESULTS="$2"
	INPUT_PDU_FILE="`mktemp`"
	echo "$COMMAND" | "$CLIENT" write > "$INPUT_PDU_FILE"
	client_raw "$COMMAND" "$EXPECTED_RESULTS" cat "$INPUT_PDU_FILE"
	rm -f "$INPUT_PDU_FILE"
}

client_multiple_start () {
	SUBTEST_NAME="$1"
	EXPECTED_RESULTS="$2"
	N="$3"
	echo "--- $SUBTEST_NAME" | tee -a response.log
	echo "--- expecting: $EXPECTED_RESULTS x$N" | tee -a response.log

	CLIENT_MULTIPLE_pids=""
	for x in `seq 1 "$N"`; do
		"$CLIENT" client_one localhost $PORT | tee -a response.log &
		CLIENT_MULTIPLE_pids="$CLIENT_MULTIPLE_pids $!"
	done
}
client_multiple_wait () {
	wait $CLIENT_MULTIPLE_pids
}


compare () {
	name="$1"
	printf >&2 "comparing \"%s\" to \"%s\"... " "$name" "$name.correct"
	if diff -u "$name.correct" "$name" > "$name.diff" 2>/dev/null; then
		echo >&2 "success."
	else
		echo >&2 "failed!"
		echo >&2 "See \"$name.diff\" for the differences."
		exit 1
	fi
}

init () {
	"$RPKI_ROOT"/proto/rcli -x -t . -y
	echo "INSERT INTO rtr_nonce VALUES ($NONCE);" | $RPKI_MYSQL_CMD

	SERIAL=""
}

make_serial () {
	PREV_SERIAL="$SERIAL"
	SERIAL="$1"
	FIRST_ASN="$2"
	LAST_ASN="$3"

	test "$FIRST_ASN" -ge 1
	test "$LAST_ASN" -le 255
	test "$FIRST_ASN" -le "$LAST_ASN"

	COMMAND_FILE="`mktemp`"

	for ASN in `seq "$FIRST_ASN" "$LAST_ASN"`; do
		for IP_LAST_OCTET in `seq 1 "$ASN"`; do
			printf 'INSERT INTO rtr_full (serial_num, asn, ip_addr) VALUES (%u, %u, '\''%u.%u.0.0/16'\'');\n' \
				"$SERIAL" "$ASN" "$ASN" "$IP_LAST_OCTET" >> "$COMMAND_FILE"
			printf 'INSERT INTO rtr_full (serial_num, asn, ip_addr) VALUES (%u, %u, '\''%u.0.%u.0/24(25)'\'');\n' \
				"$SERIAL" "$ASN" "$ASN" "$IP_LAST_OCTET" >> "$COMMAND_FILE"
			printf 'INSERT INTO rtr_full (serial_num, asn, ip_addr) VALUES (%u, %u, '\''%x::%x00/120'\'');\n' \
				"$SERIAL" "$ASN" "$ASN" "$IP_LAST_OCTET" >> "$COMMAND_FILE"
			printf 'INSERT INTO rtr_full (serial_num, asn, ip_addr) VALUES (%u, %u, '\''%x:%x::/32(127)'\'');\n' \
				"$SERIAL" "$ASN" "$ASN" "$IP_LAST_OCTET" >> "$COMMAND_FILE"
		done
	done

	if test x"$PREV_SERIAL" != x; then
		echo \
			"INSERT INTO rtr_incremental (serial_num, is_announce, asn, ip_addr)" \
			"SELECT DISTINCT $SERIAL, 1, t1.asn, t1.ip_addr" \
			"FROM rtr_full AS t1" \
			"LEFT JOIN rtr_full AS t2 ON t2.serial_num = $PREV_SERIAL AND t2.asn = t1.asn AND t2.ip_addr = t1.ip_addr" \
			"WHERE t1.serial_num = $SERIAL AND t2.serial_num IS NULL;" \
			>> "$COMMAND_FILE"
		echo \
			"INSERT INTO rtr_incremental (serial_num, is_announce, asn, ip_addr)" \
			"SELECT DISTINCT $SERIAL, 0, t1.asn, t1.ip_addr" \
			"FROM rtr_full AS t1" \
			"LEFT JOIN rtr_full AS t2 ON t2.serial_num = $SERIAL AND t2.asn = t1.asn AND t2.ip_addr = t1.ip_addr" \
			"WHERE t1.serial_num = $PREV_SERIAL AND t2.serial_num IS NULL;" \
			>> "$COMMAND_FILE"

		printf 'INSERT INTO rtr_update VALUES (%u, %u, now(), true);\n' "$SERIAL" "$PREV_SERIAL" >> "$COMMAND_FILE"
	else
		printf 'INSERT INTO rtr_update VALUES (%u, NULL, now(), true);\n' "$SERIAL" >> "$COMMAND_FILE"
	fi


	$RPKI_MYSQL_CMD < "$COMMAND_FILE"

	rm -f "$COMMAND_FILE"
}

drop_serial () {
	DROP_SERIAL="$1"

	COMMAND_FILE="`mktemp`"

	printf 'DELETE FROM rtr_update WHERE serial_num = %u;\n' "$DROP_SERIAL" >> "$COMMAND_FILE"
	printf 'DELETE FROM rtr_full WHERE serial_num = %u;\n' "$DROP_SERIAL" >> "$COMMAND_FILE"
	printf 'DELETE rtr_incremental FROM rtr_incremental LEFT JOIN rtr_update ON rtr_incremental.serial_num = rtr_update.serial_num WHERE rtr_update.prev_serial_num = %u;\n' "$DROP_SERIAL" >> "$COMMAND_FILE"
	printf 'UPDATE rtr_update SET prev_serial_num = NULL WHERE prev_serial_num = %u;\n' "$DROP_SERIAL" >> "$COMMAND_FILE"

	$RPKI_MYSQL_CMD < "$COMMAND_FILE"

	rm -f "$COMMAND_FILE"
}


start_rtrd () {
	"$SERVER" &
	SERVER_PID=$!
	sleep 1
}

stop_rtrd () {
	kill $SERVER_PID
	wait $SERVER_PID || true
}


start_test () {
	TEST="$1"

	rm -f "response.log" "response.$TEST.log"
	touch "response.log"
}

stop_test () {
	TEST="$1"

	mv -f "response.log" "response.$TEST.log"
	compare "response.$TEST.log"
}


init

start_rtrd


start_test reset_query_first
make_serial 5 1 4
client "reset_query" "all data for serial $SERIAL"
stop_test reset_query_first

start_test serial_queries
client "serial_query $WRONG_NONCE 5" "Cache Reset"
client "serial_query $NONCE 5" "empty set"
make_serial 7 2 6
client "serial_query $NONCE 5" "difference from 5 to $SERIAL"
client "serial_query $NONCE 7" "empty set"
make_serial 8 1 3
client "serial_query $NONCE 5" "difference from 5 to $SERIAL"
client "serial_query $NONCE 6" "Cache Reset"
client "serial_query $NONCE 7" "difference from 7 to $SERIAL"
client "serial_query $NONCE 8" "empty set"
drop_serial 5
client "serial_query $NONCE 5" "Cache Reset"
client "serial_query $NONCE 6" "Cache Reset"
client "serial_query $NONCE 7" "difference from 7 to $SERIAL"
client "serial_query $NONCE 8" "empty set"
drop_serial 7
client "serial_query $NONCE 7" "Cache Reset"
client "serial_query $NONCE 8" "empty set"
make_serial 14 1 3
client "serial_query $NONCE 8" "empty set"
client "serial_query $NONCE 10" "Cache Reset"
client "serial_query $NONCE 14" "empty set"
make_serial 15 1 3
client "serial_query $NONCE 8" "empty set, ending at serial $SERIAL"
make_serial 16 1 3
client "serial_query $NONCE 8" "empty set, ending at serial $SERIAL"
make_serial 17 2 3
client "serial_query $NONCE 8" "withdraw AS 1, ending at serial $SERIAL"
make_serial 18 2 3
client "serial_query $NONCE 8" "withdraw AS 1, ending at serial $SERIAL"
make_serial 19 2 3
client "serial_query $NONCE 8" "withdraw AS 1, ending at serial $SERIAL"
stop_test serial_queries

start_test bad_pdus
TOTAL_BAD_PDUS="`./badPDUs.py length`"
for i in `seq 1 "$TOTAL_BAD_PDUS"`; do
	client_raw "Bad PDU #$i" "Error Report" ./badPDUs.py "$i"
done
stop_test bad_pdus

start_test bad_pdu_usage # valid PDUs that should never be sent by the client
client "serial_notify $WRONG_NONCE 123456" "Error Report"
client "serial_notify $NONCE $SERIAL" "Error Report"
client "cache_response $WRONG_NONCE" "Error Report"
client "cache_response $NONCE" "Error Report"
client "ipv4_prefix 255 255 255 255.255.255.255 4294967295" "Error Report"
client "ipv6_prefix 255 255 255 ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff 4294967295" "Error Report"
client "ipv4_prefix 1 32 32 255.255.255.255 4294967295" "Error Report"
client "ipv6_prefix 1 128 128 ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff 4294967295" "Error Report"
client "ipv4_prefix 0 0 0 0.0.0.0 0" "Error Report"
client "ipv6_prefix 0 0 0 :: 0" "Error Report"
client "end_of_data $WRONG_NONCE 123456" "Error Report"
client "end_of_data $NONCE $SERIAL" "Error Report"
client "cache_reset" "Error Report"
client "error_report 2" "no response" # No Data Available
client "error_report 3" "no response" # Invalid Request
stop_test bad_pdu_usage

start_test bad_pdu_sequence # valid PDUs that can be send by the client, but are sent at the wrong time or indicate an error
client "error_report 0" "no response" # Corrupt Data
client "error_report 1" "no response" # Internal Error
client "error_report 4" "no response" # Unsupported Protocol Version
client "error_report 5" "no response" # Unsupported PDU Type
client "error_report 6" "no response" # Withdrawal of Unknown Record
client "error_report 7" "no response" # Duplicate Announcement Received
stop_test bad_pdu_sequence

start_test serial_notify
client_multiple_start "serial_notify" "Serial Notify for serial 20" 5
make_serial 20 1 3
client_multiple_wait
stop_test serial_notify

start_test reset_query_last
client "reset_query" "all data for serial $SERIAL"
stop_test reset_query_last


stop_rtrd
