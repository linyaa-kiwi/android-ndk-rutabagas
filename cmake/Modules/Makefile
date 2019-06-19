CMAKE_MODULES := \
    RuAddCFlag.cmake \
    RuAddCXXFlag.cmake \
    RuCheckCAttribute.cmake \
    RuCheckCXXAttribute.cmake \
    $(NULL)

.PHONY: all
all: $(CMAKE_MODULES)
	@

.PHONY: clean
clean:
	$(RM) $(CMAKE_MODULES)

RuAddCFlag.cmake: RuAddCompilerFlagsGen.py
	./$< --lang=c > $@

RuAddCXXFlag.cmake: RuAddCompilerFlagsGen.py
	./$< --lang=c++ > $@

RuCheckCAttribute.cmake: RuCheckCompilerAttributeGen.py
	./$< --lang=c > $@

RuCheckCXXAttribute.cmake: RuCheckCompilerAttributeGen.py
	./$< --lang=c++ > $@
