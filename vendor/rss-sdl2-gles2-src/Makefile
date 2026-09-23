.PHONY: all native browser savers libs clean gallery help-gen

SAVERS_DIR = savers
SAVERS = testsaver flux cyclone solarwinds fieldlines euphoria flocks plasma helios lattice hyperspace microcosm skyrocket implicitdemo

LIBS_DIR = libs
LIBS = gl4es glues rsmath librs

LOG_DIR = logs

# Build one target ($2) in one subdir ($1/$3), tee to a log file, and
# remember the exit code. Always returns 0 from the outer command so
# the for loop continues past failures; failures are surfaced in the
# summary by reading the .rc file. (From sgi-demos.)
#
# $1 = subdir family (savers or libs)
# $2 = make target (empty, native, browser, clean)
# $3 = item name (e.g. flux, gl4es)
define build_one
    mkdir -p $(LOG_DIR) ; \
    LOG=$(LOG_DIR)/$(3).log ; \
    echo "" ; echo "BUILDING: $(3)" ; echo "" ; \
    ( make $(2) -C $(1)/$(3) 2>&1 ; echo $$? > $(LOG_DIR)/$(3).rc ) | tee $$LOG ;
endef

# Summarize all .log/.rc files in $(LOG_DIR).
define summarize
    @echo "" ; \
    echo "==================== BUILD SUMMARY ====================" ; \
    printf "%-15s %12s %12s %13s\n" "TARGET" "ERRORS" "WARNINGS" "STATUS" ; \
    fail=0 ; \
    for item in $(1) ; do \
        log=$(LOG_DIR)/$$item.log ; \
        rc_file=$(LOG_DIR)/$$item.rc ; \
        if [ -f $$log ] ; then \
            errs=$$(grep -c 'error:' $$log 2>/dev/null) ; errs=$${errs:-0} ; \
            warns=$$(grep -c 'warning:' $$log 2>/dev/null) ; warns=$${warns:-0} ; \
        else \
            errs=? ; warns=? ; \
        fi ; \
        if [ -f $$rc_file ] ; then \
            rc=$$(cat $$rc_file) ; \
        else \
            rc=? ; \
        fi ; \
        if [ "$$rc" != "0" ] ; then fail=1 ; fi ; \
        printf "%-15s %12s %12s %13s\n" $$item $$errs $$warns $$rc ; \
    done ; \
    echo "=======================================================" ; \
    if [ $$fail -ne 0 ] ; then \
        echo "BUILD FAILED: See $(LOG_DIR)/<target>.log for details." ; \
        exit 1 ; \
    else \
        echo "BUILD OK: All targets built." ; \
    fi
endef

all:
	@rm -rf $(LOG_DIR)
	@for lib in $(LIBS) ; do $(call build_one,$(LIBS_DIR),native,$$lib) done
	@for saver in $(SAVERS) ; do $(call build_one,$(SAVERS_DIR),native,$$saver) done
	$(call summarize,$(LIBS))
	$(call summarize,$(SAVERS))

native:
	@rm -rf $(LOG_DIR)
	@for saver in $(SAVERS) ; do $(call build_one,$(SAVERS_DIR),native,$$saver) done
	$(call summarize,$(SAVERS))

browser:
	@rm -rf $(LOG_DIR)
	@for lib in $(LIBS) ; do $(call build_one,$(LIBS_DIR),browser,$$lib) done
	@for saver in $(SAVERS) ; do $(call build_one,$(SAVERS_DIR),browser,$$saver) done
	$(call summarize,$(LIBS))
	$(call summarize,$(SAVERS))
	$(MAKE) gallery

# Assemble web/ with every saver's html/js/wasm + a gallery index.html
gallery:
	@scripts/deploy-web.sh

# Regenerate each saver's rss_help.cpp (the --help option table) from its
# handleCommandLine(). Run after editing a saver's options.
help-gen:
	@python3 scripts/gen_help.py --all

libs:
	@rm -rf $(LOG_DIR)
	@for lib in $(LIBS) ; do $(call build_one,$(LIBS_DIR),native,$$lib) done
	$(call summarize,$(LIBS))

clean:
	@for lib in $(LIBS) ; do make clean -C $(LIBS_DIR)/$$lib ; done
	@for saver in $(SAVERS) ; do make clean -C $(SAVERS_DIR)/$$saver ; done
	@rm -rf $(LOG_DIR)
