## Anything that uses $(LOG_COMPILER) before "make all" finishes should
## depend on this.
LOG_COMPILER_DEPS = \
	tests/setup_test_environment.sh \
	tests/test.include

LOG_COMPILER = $(abs_top_builddir)/tests/setup_test_environment.sh


EXTRA_DIST += etc/test.conf

check_SCRIPTS += tests/setup_test_environment.sh
MK_SUBST_FILES_EXEC += tests/setup_test_environment.sh
tests/setup_test_environment.sh: $(srcdir)/tests/setup_test_environment.sh.in

check_DATA += tests/test.conf
MK_SUBST_FILES += tests/test.conf
tests/test.conf: $(srcdir)/tests/test.conf.in

noinst_DATA += tests/test.include
MK_SUBST_FILES += tests/test.include
tests/test.include: $(srcdir)/tests/test.include.in


# When $CHECKTOOL is set, extra log files can be generated. This rule cleans up
# those log files.
clean-local: clean-local-checktool-logs
.PHONY: clean-local-checktool-logs
clean-local-checktool-logs:
	find . -type f -name 'valgrind.*.log' -exec rm -f '{}' +


# Cat all the log files produced by self-tests.
# This is probably only useful to capture the log files in an automated build
# and test environment.
.PHONY: cat-logs
cat-logs:
	{ \
		for f in $(TEST_LOGS); do \
			echo "$$f"; \
			echo "$(distdir)/_build/$$f"; \
		done; \
		find . -type f -name 'valgrind.*.log' -print; \
	} | sort | uniq | while read log_file; do \
		if test -f "$$log_file"; then \
			echo "++++++ $$log_file ++++++"; \
			echo; \
			cat "$$log_file"; \
			echo; \
			echo; \
		fi; \
	done
