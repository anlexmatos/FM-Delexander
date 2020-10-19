#include "plugin.hpp"
#include "GraphData.hpp"
#include <bitset>

constexpr float BLINK_INTERVAL = 0.42857142857f;
constexpr float DEF_CLICK_FILTER_SLEW = 3750.f;
constexpr float FIVE_D_THREE = 5.f / 3.f;
constexpr float CLOCK_IGNORE_DURATION = 0.001f;// disable clock on powerup and reset for 1 ms (so that the first step plays)
constexpr float DEF_RED_BRIGHTNESS = 0.4975f;
constexpr float INDICATOR_BRIGHTNESS = 0.325f;

struct Algomorph4 : Module {
    enum ParamIds {
        ENUMS(OPERATOR_BUTTONS, 4),
        ENUMS(MODULATOR_BUTTONS, 4),
        ENUMS(SCENE_BUTTONS, 3),
        MORPH_KNOB,
        EDIT_BUTTON,
        SCREEN_BUTTON,
        NUM_PARAMS
    };
    enum InputIds {
        ENUMS(OPERATOR_INPUTS, 4),
        MORPH_INPUT,
        SCENE_ADV_INPUT,    //Clk input
        RESET_INPUT,
        OPTION_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        ENUMS(MODULATOR_OUTPUTS, 4),
        SUM_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        ENUMS(DISPLAY_BACKLIGHT, 3),        // 3 colors
        ENUMS(SCENE_LIGHTS, 9),             // 3 colors per light
        ENUMS(H_CONNECTION_LIGHTS, 12),     // 3 colors per light
        ENUMS(D_DISABLE_LIGHTS, 12),
        ENUMS(CONNECTION_LIGHTS, 36),       // 3 colors per light
        ENUMS(OPERATOR_LIGHTS, 12),         // 3 colors per light
        ENUMS(MODULATOR_LIGHTS, 12),        // 3 colors per light
        EDIT_LIGHT,
        KNOB_LIGHT,
        GLOWING_INK,
        ONE_LIGHT,
        TWO_LIGHT,
        THREE_LIGHT,
        ENUMS(SCREEN_BUTTON_RING_LIGHT, 3),     // 3 colors
        SCREEN_BUTTON_LIGHT,
        ENUMS(CARRIER_INDICATORS, 12),      //3 colors per light
        NUM_LIGHTS
    };
    struct OptionInput {
        enum Modes {
            MORPH_CV,
            MORPH_ATTEN,
            RUN,
            CLOCK,
            REVERSE_CLOCK,
            RESET,
            MOD_ATTEN,
            OP_ATTEN,
            SUM_ATTEN,
            SCENE_OFFSET,
            WILDCARD_MOD,
            WILDCARD_SUM,
            ENUMS(SHADOW, 4),       //Shadow 
            CLICK_FILTER,
            TRIPLE_MORPH_CV,
            SCREEN_BRIGHTNESS,
            CONNECTION_BRIGHTNESS,
            RING_BRIGHTNESS,
            ALL_BRIGHTNESS,
            NUM_MODES
        };
        bool mode[NUM_MODES] = {false};
        bool isAudioMode[NUM_MODES] = {false};
        bool allowMultipleModes = false;
        int activeModes = 0;
        Modes lastSetMode;
        bool forgetVoltage = true;
        bool connected = false;
        Algomorph4* module;
        dsp::SchmittTrigger runCVTrigger;
        dsp::SchmittTrigger sceneAdvCVTrigger;
        dsp::SchmittTrigger reverseSceneAdvCVTrigger;
        dsp::SlewLimiter shadowOpClickFilters[2][4][5][16];     // [noRing/ring][shadow op][legal mod][channel], clickGain[x][y][3][z] = horizontal connection, clickGain[x][y][4][z] = sum output
        float shadowOpClickGains[2][4][5][16] = {{{0.f}}};
        dsp::SlewLimiter wildcardModClickFilter[16];
        float wildcardModClickGain = 0.f;
        dsp::SlewLimiter wildcardSumClickFilter[16];
        float wildcardSumClickGain = 0.f;
        float voltage[NUM_MODES][16];
        float defVoltage[NUM_MODES] = { 0.f };
        int channels = 0;

        OptionInput(Algomorph4* m) {
            module = m;
            defVoltage[MORPH_ATTEN] = 5.f;
            defVoltage[MOD_ATTEN] = 5.f;
            defVoltage[OP_ATTEN] = 5.f;
            defVoltage[SUM_ATTEN] = 5.f;
            resetVoltages();
            for (int i = WILDCARD_MOD; i < CLICK_FILTER; i++)
                isAudioMode[i] = true;
            for (int c = 0; c < 16; c++) {
                wildcardModClickFilter[c].setRiseFall(DEF_CLICK_FILTER_SLEW, DEF_CLICK_FILTER_SLEW);
                wildcardSumClickFilter[c].setRiseFall(DEF_CLICK_FILTER_SLEW, DEF_CLICK_FILTER_SLEW);
                for (int i = 0; i < 2; i++) {
                    for (int j = 0; j < 4; j++) {
                        for (int k = 0; k < 5; k++)
                            shadowOpClickFilters[i][j][k][c].setRiseFall(DEF_CLICK_FILTER_SLEW, DEF_CLICK_FILTER_SLEW);
                    }
                }
            }
        }

        void forgetVoltages() {
            for (int i = 0; i < NUM_MODES; i++) {
                for (int j = 0; j < 16; j++) {
                    if (!mode[i])
                        voltage[i][j] = defVoltage[i];
                }
            }
        }
        
        void resetVoltages() {
            for (int i = 0; i < NUM_MODES; i++) {
                for (int j = 0; j < 16; j++) {
                    voltage[i][j] = defVoltage[i];
                }
            }
        }

        void toggleAllowMultipleModes() {
            if (allowMultipleModes) {
                if (activeModes > 1) {
                    for (int i = 0; i < NUM_MODES; i++) {
                        if (mode[i] && i != lastSetMode)
                            unsetMode(static_cast<Modes>(i));
                    }
                }
                allowMultipleModes = false;
            }
            else
                allowMultipleModes = true;
        }

        void setMode(Modes newMode) {
            activeModes++;
            if (!allowMultipleModes) {
                for (int i = 0; i < NUM_MODES; i++) {
                    if (mode[i])   //Unset previous mode
                        unsetMode(static_cast<Modes>(i));
                }
            }
            mode[newMode] = true;
            module->inputs[OPTION_INPUT].readVoltages(voltage[newMode]);
            lastSetMode = newMode;
        }

        void unsetMode(Modes oldMode) {
            if (activeModes > 1) {
                if (forgetVoltage || isAudioMode[oldMode]) {
                    for (int c = 0; c < channels; c++)
                        voltage[oldMode][c] = defVoltage[oldMode];
                }
                mode[oldMode] = false;
                activeModes--;
            }
        }

        void toggleForgetVoltage() {
            if (forgetVoltage)
                forgetVoltage = false;
            else {
                forgetVoltages();
                forgetVoltage = true;
            }
        }

        void updateVoltage() {
            for (int i = 0; i < NUM_MODES; i++) {
                if (mode[i]) {
                    for (int c = 0; c < channels; c++)
                        voltage[i][c] = module->inputs[OPTION_INPUT].getPolyVoltage(c);
                }
            }
        }
    };
    OptionInput optionInput = OptionInput(this);
    int baseScene = 1;                  // Center the Morph knob on saved algorithm 0, 1, or 2
    float morph[16] = {0.f};            // Range -1.f -> 1.f
    float relativeMorphMagnitude[16] = { morph[0] };
    int centerMorphScene[16] = { baseScene }, forwardMorphScene[16] = { (baseScene + 1) % 3 }, backwardMorphScene[16] = { (baseScene + 2) % 3 };
    bool horizontalDestinations[3][4];  // [scene][op]
    bool opDestinations[3][4][3];       // [scene][op][legal mod]
    bool forcedCarrier[3][4];           // [scene][op]
    std::bitset<12> algoName[3];        // 12-bit IDs of the three stored algorithms
    int sixteenToTwelve[4089];          // Graph ID conversion
                                        // The algorithm graph data are stored with IDs in 12-bit space:
                                        //       000 000 000 000 -> 111 111 111 000
                                        // Each set of 3 bits corresponds to an operator.
                                        // Each bit represents an oscillator's "legal" mod destinations.
                                        // At least one operator is a carrier (no mod destinations, all bits zero).
                                        // However, the algorithms are accessed via 16-bit IDs:
                                        //       0000 0000 0000 0000 -> 1110 1101 1011 0000
                                        // In 16-bit space, the the feedback destinations are included but never equal 1.  
                                        // sixteenToTwelve is indexed by 16-bit ID and returns equivalent 12-bit ID.
    int threeToFour[4][3];              // Modulator ID conversion ([op][x] = y, where x is 0..2 and y is 0..3)
    bool configMode = true;
    int configOp = -1;                  // Set to 0-3 when configuring mod destinations for operators 1-4
    int configScene = 1;
    bool running = true;

    float opButtonPressed[4] = {0};
    bool noReaction[4] = {true, true, true, true};

    bool graphDirty = true;
    bool debug = false;

    //User settings
    bool clickFilterEnabled = true;
    bool ringMorph = false;
    bool randomRingMorph = false;
    bool randomAllowHorizontal = false;
    bool exitConfigOnConnect = false;
    bool ccwSceneSelection = true;      // Default true to interface with rising ramp LFO at Morph CV input
    bool glowingInk = false;
    bool vuLights = true;
    bool runSilencer = true;
    bool resetOnRun = false;
    bool horizontalAllowed = false;
    int resetScene = 1;
    float clickFilterSlew = DEF_CLICK_FILTER_SLEW;

    dsp::SchmittTrigger sceneAdvCVTrigger;
    dsp::SchmittTrigger resetCVTrigger;
    long clockIgnoreOnReset = (long) (CLOCK_IGNORE_DURATION * APP->engine->getSampleRate());

    dsp::ClockDivider cvDivider;
    dsp::BooleanTrigger sceneButtonTrigger[3];
    dsp::BooleanTrigger editTrigger;
    dsp::BooleanTrigger operatorTrigger[4];
    dsp::BooleanTrigger modulatorTrigger[4];

    dsp::SlewLimiter runClickFilter;
    dsp::SlewLimiter clickFilters[2][4][5][16];    // [noRing/ring][op][legal mod][channel], [op][3] = Sum output
    dsp::ClockDivider clickFilterDivider;

    dsp::ClockDivider lightDivider;
    float sceneBrightnesses[3][12][3] = {{{}}};
    float blinkTimer = BLINK_INTERVAL;
    bool blinkStatus = true;

    Algomorph4() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(MORPH_KNOB, -1.f, 1.f, 0.f, "Algorithm Morph", "", 0, 100);
        for (int i = 0; i < 4; i++) {
            configParam(OPERATOR_BUTTONS + i, 0.f, 1.f, 0.f);
            configParam(MODULATOR_BUTTONS + i, 0.f, 1.f, 0.f);
        }
        for (int i = 0; i < 3; i++) {
            configParam(SCENE_BUTTONS + i, 0.f, 1.f, 0.f);
        }
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 5; j++) {
                for (int c = 0; c < 16; c++) {
                    clickFilters[0][i][j][c].setRiseFall(DEF_CLICK_FILTER_SLEW, DEF_CLICK_FILTER_SLEW);
                    clickFilters[1][i][j][c].setRiseFall(DEF_CLICK_FILTER_SLEW, DEF_CLICK_FILTER_SLEW);
                }
            }
        }
        runClickFilter.setRiseFall(400.f, 400.f);
        clickFilterDivider.setDivision(128);
        lightDivider.setDivision(64);
        cvDivider.setDivision(32);

        // Map 3-bit operator-relative mod output indices to 4-bit generalized equivalents
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (i != j) {
                    threeToFour[i][i > j ? j : j - 1] = j;
                }
            }
        }

        // Initialize sixteenToTwelve[] to -1, then index 9-bit IDs by the 12-bit equivalents
        for (int i = 0; i < 4089; i++) {
            sixteenToTwelve[i] = -1;
        }
        for (int i = 0; i < 1695; i++) {
            sixteenToTwelve[(int)xNodeData[i][0]] = i;
        }

        onReset();
    }

    void onReset() override {
        for (int i = 0; i < 3; i++) {
            algoName[i].reset();
            for (int j = 0; j < 4; j++) {
                horizontalDestinations[i][j] = false;
                forcedCarrier[i][j] = false;
                for (int k = 0; k < 3; k++) {
                    opDestinations[i][j][k] = false;
                }
            }
        }
        optionInput.setMode(OptionInput::WILDCARD_MOD);
        optionInput.resetVoltages();
        optionInput.allowMultipleModes = false;
        optionInput.forgetVoltage = true;
        configMode = false;
        configOp = -1;
        configScene = 1;
        baseScene = 1;
        clickFilterEnabled = true;
        clickFilterSlew = DEF_CLICK_FILTER_SLEW;
        ringMorph = false;
        randomRingMorph = false;
        randomAllowHorizontal = false;
        exitConfigOnConnect = false;
        ccwSceneSelection = true;
        running = true;
        runSilencer = true;
        resetOnRun = false;
        resetScene = 1;
        horizontalAllowed = false;

        blinkStatus = true;
        blinkTimer = BLINK_INTERVAL;
        graphDirty = true;
    }

    void onRandomize() override {
        //If in config mode, only randomize the current algorithm,
        //do not change the base scene and do not enable/disable ring morph
        if (configMode) {
            bool noCarrier = true;
            algoName[configScene].reset();    //Initialize
            for (int j = 0; j < 4; j++) {
                horizontalDestinations[configScene][j] = false;   //Initialize
                forcedCarrier[configScene][j] = false;   //Initialize
                if (random::uniform() > .5f) {
                    forcedCarrier[configScene][j] = true;
                    noCarrier = false;
                }
                horizontalDestinations[configScene][j] = random::uniform() > .5f;
                if (horizontalAllowed) {
                    for (int k = 0; k < 3; k++) {
                        opDestinations[configScene][j][k] = false;    //Initialize
                        if (random::uniform() > .5f) {
                            opDestinations[configScene][j][k] = true;
                            algoName[configScene].flip(j * 3 + k);    
                        }
                    }
                }
                else {
                    if (!horizontalDestinations[configScene][j]) {
                        for (int k = 0; k < 3; k++) {
                            opDestinations[configScene][j][k] = false;    //Initialize
                            if (random::uniform() > .5f) {
                                opDestinations[configScene][j][k] = true;
                                algoName[configScene].flip(j * 3 + k);    
                            }
                        }
                    }
                    else {
                        for (int k = 0; k < 3; k++)
                            opDestinations[configScene][j][k] = false;
                    }
                }
            }
            if (noCarrier) {
                int shortStraw = std::floor(random::uniform() * 4);
                while (shortStraw == 4)
                    shortStraw = std::floor(random::uniform() * 4);
                forcedCarrier[configScene][shortStraw] = true;
                horizontalDestinations[configScene][shortStraw] = false;
                for (int k = 0; k < 3; k++) {
                    opDestinations[configScene][shortStraw][k] = false;
                    algoName[configScene].set(shortStraw * 3 + k, false);
                }
            }
        }
        //Otherwise, randomize everything
        else {
            for (int i = 0; i < 3; i++) {
                bool noCarrier = true;
                algoName[i].reset();    //Initialize
                for (int j = 0; j < 4; j++) {
                    horizontalDestinations[i][j] = false;   //Initialize
                    forcedCarrier[configScene][j] = false;   //Initialize
                    if (random::uniform() > .5f) {
                        forcedCarrier[i][j] = true;
                        noCarrier = false;
                    }
                    horizontalDestinations[i][j] = random::uniform() > .5f;
                    if (horizontalAllowed) {
                        for (int k = 0; k < 3; k++) {
                            opDestinations[i][j][k] = false;    //Initialize
                            if (random::uniform() > .5f) {
                                opDestinations[i][j][k] = true;
                                algoName[i].flip(j * 3 + k);    
                            }
                        }
                    }
                    else {
                        if (!horizontalDestinations[i][j]) {
                            for (int k = 0; k < 3; k++) {
                                opDestinations[i][j][k] = false;    //Initialize
                                if (random::uniform() > .5f) {
                                    opDestinations[i][j][k] = true;
                                    algoName[i].flip(j * 3 + k);    
                                }
                            }
                        }
                        else {
                            for (int k = 0; k < 3; k++)
                                opDestinations[i][j][k] = false;
                        }
                    }
                }
                if (noCarrier) {
                    int shortStraw = std::floor(random::uniform() * 4);
                    while (shortStraw == 4)
                        shortStraw = std::floor(random::uniform() * 4);
                    forcedCarrier[i][shortStraw] = true;
                    horizontalDestinations[i][shortStraw] = false;
                    for (int k = 0; k < 3; k++) {
                        opDestinations[i][shortStraw][k] = false;
                        algoName[i].set(shortStraw * 3 + k, false);
                    }
                }
            }
            baseScene = 1;
            if (randomRingMorph)
                ringMorph = random::uniform() > .5f;
            Module::onRandomize();
        }
        graphDirty = true;
    }

    void process(const ProcessArgs& args) override {
        float in[16] = {0.f};                                   // Operator input channels
        float modOut[4][16] = {{0.f}};                          // Modulator outputs & channels
        float sumOut[16] = {0.f};                               // Sum output channels
        float clickGain[2][4][5][16] = {{{{0.f}}}};             // Click filter gains:  [noRing/ring][op][legal mod][channel], clickGain[x][y][3][z] = sum output
        bool carrier[3][4] = {  {true, true, true, true},       // Per-algorithm operator carriership status
                                {true, true, true, true},       // [scene][op]
                                {true, true, true, true} };
        int sceneOffset[16] = {0};                               //Offset to the base scene
        int channels = 1;                                       // Max channels of operator inputs

        if (inputs[OPTION_INPUT].isConnected()) {
            optionInput.connected = true;
            optionInput.updateVoltage();
        }
        else {
            if (optionInput.connected) {
                optionInput.connected = false;
                optionInput.resetVoltages();
                running = true;
            }
        }

        //Determine polyphony count
        for (int i = 0; i < 4; i++) {
            if (channels < inputs[OPERATOR_INPUTS + i].getChannels())
                channels = inputs[OPERATOR_INPUTS + i].getChannels();
        }
        if (channels < inputs[MORPH_INPUT].getChannels())
            channels = inputs[MORPH_INPUT].getChannels();
        if (channels < inputs[OPTION_INPUT].getChannels())
            channels = inputs[OPTION_INPUT].getChannels();
        optionInput.channels = channels;

        if (cvDivider.process()) {
            //Reset trigger
            if (resetCVTrigger.process(inputs[RESET_INPUT].getVoltage() + optionInput.voltage[OptionInput::RESET][0])) {
                initRun();// must be after sequence reset
                sceneAdvCVTrigger.reset();
            }

            //Run trigger
            if (optionInput.runCVTrigger.process(optionInput.voltage[OptionInput::RUN][0])) {
                running ^= true;
                if (running) {
                    if (resetOnRun)
                        initRun();
                }
            }

            //Check to change scene
            //Option input
            for (int c = 0; c < channels; c++) {
                float sceneOffsetVoltage = optionInput.voltage[OptionInput::SCENE_OFFSET][c];
                if (sceneOffsetVoltage > FIVE_D_THREE)
                    sceneOffset[c] = 1;
                else if (sceneOffsetVoltage < -FIVE_D_THREE)
                    sceneOffset[c] = 2;
                else
                    sceneOffset[c] = 0;
            }
            //Scene buttons
            for (int i = 0; i < 3; i++) {
                if (sceneButtonTrigger[i].process(params[SCENE_BUTTONS + i].getValue() > 0.f)) {
                    if (configMode) {
                        //If not changing to a new scene
                        if (configScene == i) {
                            //Exit config mode
                            configMode = false;
                        }
                        else {
                            //Switch scene
                            configScene = i;
                        }
                    }
                    else {
                        //If not changing to a new scene
                        if (baseScene == i) {
                            //Reset morph knob
                            params[MORPH_KNOB].setValue(0.f);
                        }
                        else {
                            //Switch scene
                            baseScene = i;
                        }
                    }
                    graphDirty = true;
                }
            }

            if (running && clockIgnoreOnReset == 0l) {
                //Scene advance trigger input
                if (sceneAdvCVTrigger.process(inputs[SCENE_ADV_INPUT].getVoltage())) {
                    //Advance base scene
                    if (!ccwSceneSelection)
                    baseScene = (baseScene + 1) % 3;
                    else
                    baseScene = (baseScene + 2) % 3;
                    graphDirty = true;
                }
                if (optionInput.sceneAdvCVTrigger.process(optionInput.voltage[OptionInput::CLOCK][0])) {
                    //Advance base scene
                    if (!ccwSceneSelection)
                    baseScene = (baseScene + 1) % 3;
                    else
                    baseScene = (baseScene + 2) % 3;
                    graphDirty = true;
                }
                if (optionInput.reverseSceneAdvCVTrigger.process(optionInput.voltage[OptionInput::REVERSE_CLOCK][0])) {
                    //Advance base scene
                    if (!ccwSceneSelection)
                    baseScene = (baseScene + 2) % 3;
                    else
                    baseScene = (baseScene + 1) % 3;
                    graphDirty = true;
                }
            }

            //Edit button
            if (editTrigger.process(params[EDIT_BUTTON].getValue() > 0.f)) {
                configMode ^= true;
                if (configMode) {
                    blinkStatus = true;
                    blinkTimer = 0.f;
                    if (morph[0] > .5f)
                        configScene = (baseScene + sceneOffset[0] + 1) % 3;
                    else if (morph[0] < -.5f)
                        configScene = (baseScene + sceneOffset[0] + 2) % 3;
                    else
                        configScene = baseScene + sceneOffset[0];
                }
                configOp = -1;
            }

            //Check to select/deselect operators
            for (int i = 0; i < 4; i++) {
                if (operatorTrigger[i].process(params[OPERATOR_BUTTONS + i].getValue() > 0.f)) {
                    if (params[OPERATOR_BUTTONS + i].getValue() > 0.f) {
                        if (!configMode) {
                            configMode = true;
                            configOp = i;
                            if (morph[0] > .5f)
                                configScene = (baseScene + sceneOffset[0] + 1) % 3;
                            else if (morph[0] < -.5f)
                                configScene = (baseScene + sceneOffset[0] + 2) % 3;
                            else
                                configScene = baseScene + sceneOffset[0];
                            blinkStatus = true;
                            blinkTimer = 0.f;
                        }
                        else if (configOp == i) {  
                            //Deselect operator
                            configOp = -1;
                            configMode = false;
                        }
                        else {
                            configOp = i;
                            blinkStatus = true;
                            blinkTimer = 0.f;
                        }
                        graphDirty = true;
                        break;
                    }
                }
            }

            //Check for config mode destination selection and forced operator designation
            if (configMode) {
                if (configOp > -1) {
                    if (modulatorTrigger[configOp].process(params[MODULATOR_BUTTONS + configOp].getValue() > 0.f)) {  //Op is connected to itself
                        horizontalDestinations[configScene][configOp] ^= true;

                        if (exitConfigOnConnect)
                            configMode = false;
                        
                        graphDirty = true;
                    }
                    else {
                        for (int i = 0; i < 3; i++) {
                            if (modulatorTrigger[threeToFour[configOp][i]].process(params[MODULATOR_BUTTONS + threeToFour[configOp][i]].getValue() > 0.f)) {

                                opDestinations[configScene][configOp][i] ^= true;
                                algoName[configScene].flip(configOp * 3 + i);

                                if (exitConfigOnConnect)
                                    configMode = false;

                                graphDirty = true;
                                break;
                            }
                        }
                    }
                }
                else {
                    for (int i = 0; i < 4; i++) {
                        if (modulatorTrigger[i].process(params[MODULATOR_BUTTONS + i].getValue() > 0.f)) {
                            forcedCarrier[configScene][i] ^= true;
                            break;
                        }
                    }
                }
            }
            else {
                for (int i = 0; i < 4; i++) {
                    if (modulatorTrigger[i].process(params[MODULATOR_BUTTONS + i].getValue() > 0.f)) {
                        forcedCarrier[centerMorphScene[0]][i] ^= true;
                        break;
                    }
                }
            }
        }
        
        //Update clickfilter rise/fall times
        if (clickFilterDivider.process()) {
            if (optionInput.mode[OptionInput::CLICK_FILTER]) {
                for (int c = 0; c < 16; c++) {
                    //+/-5V = 0V-2V
                    float clickFilterGain = (clamp(optionInput.voltage[OptionInput::CLICK_FILTER][c] / 5.f, -1.f, 1.f) + 1.001f);
                    float clickFilterResult = clickFilterSlew * clickFilterGain;
                    optionInput.wildcardModClickFilter[c].setRiseFall(clickFilterResult, clickFilterResult);
                    optionInput.wildcardSumClickFilter[c].setRiseFall(clickFilterResult, clickFilterResult);
                    for (int i = 0; i < 4; i++) {
                        for (int j = 0; j < 4; j++) {
                            clickFilters[0][i][j][c].setRiseFall(clickFilterResult, clickFilterResult);
                            clickFilters[1][i][j][c].setRiseFall(clickFilterResult, clickFilterResult);
                            optionInput.shadowOpClickFilters[0][i][j][c].setRiseFall(clickFilterResult, clickFilterResult);
                            optionInput.shadowOpClickFilters[1][i][j][c].setRiseFall(clickFilterResult, clickFilterResult);
                        }
                    }
                }
            }
        }

        //  Update morph status
        // Only redraw display if morph on channel 1 has changed
        float morphAttenInput = clamp(optionInput.voltage[OptionInput::MORPH_ATTEN][0] / 5.f, -1.f, 1.f);
        float newMorph0 =  clamp(inputs[MORPH_INPUT].getVoltage(0) / 5.f, -1.f, 1.f)
                            * morphAttenInput
                            + params[MORPH_KNOB].getValue()
                            + clamp(optionInput.voltage[OptionInput::MORPH_CV][0] / 5.f, -1.f, 1.f)
                            + clamp(optionInput.voltage[OptionInput::TRIPLE_MORPH_CV][0] / FIVE_D_THREE, -3.f, 3.f);
        newMorph0 = clamp(newMorph0, -3.f, 3.f);
        if (morph[0] != newMorph0) {
            morph[0] = newMorph0;
            graphDirty = true;
        }

        for (int c = 1; c < channels; c++)
            morph[c] = clamp(inputs[MORPH_INPUT].getPolyVoltage(c) / 5.f, -1.f, 1.f) * morphAttenInput + params[MORPH_KNOB].getValue() + clamp(optionInput.voltage[OptionInput::MORPH_CV][c] / 5.f, -1.f, 1.f);

        if (!ringMorph) {
            for (int c = 0; c < channels; c++) {
                relativeMorphMagnitude[c] = morph[c];
                if (morph[c] > 0.f) {
                    if (morph[c] < 1.f) {
                        centerMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                    }
                    else if (morph[c] == 1.f) {
                        relativeMorphMagnitude[c] = 0.f;
                        centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                    }
                    else if (morph[c] < 2.f) {
                        relativeMorphMagnitude[c] -= 1.f;
                        centerMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                    }
                    else if (morph[c] == 2.f) {
                        relativeMorphMagnitude[c] = 0.f;
                        centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                    }
                    else if (morph[c] < 3.f) {
                        relativeMorphMagnitude[c] -= 2.f;
                        centerMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                    }
                    else {
                        relativeMorphMagnitude[c] = 0.f;
                        centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                    }
                }
                else if (morph[c] == 0.f)
                    centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                else {
                    relativeMorphMagnitude[c] *= -1.f;
                    if (morph[c] > -1.f) {
                        centerMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                    }
                    else if (morph[c] == -1.f) {
                        relativeMorphMagnitude[c] = 0.f;
                        centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                    }
                    else if (morph[c] > -2.f) {
                        relativeMorphMagnitude[c] -= 1.f;
                        centerMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                    }
                    else if (morph[c] == -2.f) {
                        relativeMorphMagnitude[c] = 0.f;
                        centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                    }
                    else if (morph[c] < 3.f) {
                        relativeMorphMagnitude[c] -= 2.f;
                        centerMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                    }
                    else {
                        relativeMorphMagnitude[c] = 0.f;
                        centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                    }
                }
            }
        }
        else {
            for (int c = 0; c < channels; c++) {
                relativeMorphMagnitude[c] = morph[c];
                if (morph[c] > 0.f) {
                    if (morph[c] <= 1.f) {
                        centerMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                    }
                    else if (morph[c] < 2.f) {
                        relativeMorphMagnitude[c] -= (relativeMorphMagnitude[c] - 1.f) * 2.f;
                        centerMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                    }
                    else if (morph[c] == 2.f) {
                        relativeMorphMagnitude[c] = 0.f;
                        centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                    }
                    else if (morph[c] <= 3.f) {
                        relativeMorphMagnitude[c] -= (relativeMorphMagnitude[c] - 1.f) * 2.f;
                        centerMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                    }
                }
                else if (morph[c] == 0.f)
                    centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                else {
                    relativeMorphMagnitude[c] *= -1.f;
                    if (morph[c] >= -1.f) {
                        centerMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                    }
                    else if (morph[c] > -2.f) {
                        relativeMorphMagnitude[c] -= (relativeMorphMagnitude[c] - 1.f) * 2.f;
                        centerMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                    }
                    else if (morph[c] == -2.f) {
                        relativeMorphMagnitude[c] = 0.f;
                        centerMorphScene[c] = forwardMorphScene[c] = backwardMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                    }
                    else if (morph[c] <= 3.f) {
                        relativeMorphMagnitude[c] -= (relativeMorphMagnitude[c] - 1.f) * 2.f;
                        relativeMorphMagnitude[c] *= -1.f;
                        centerMorphScene[c] = (baseScene + sceneOffset[c]) % 3;
                        forwardMorphScene[c] = (baseScene + sceneOffset[c] + 1) % 3;
                        backwardMorphScene[c] = (baseScene + sceneOffset[c] + 2) % 3;
                    }
                }
            }
            
        }

        //Get operator input channel then route to modulation output channel or to sum output channel
        float wildcardMod[16] = {0.f};
        float wildcardSum[16] = {0.f};
        float shadowOp[16] = {0.f};
        float modAttenuversion[16] = {1.f};
        float opAttenuversion[16] = {1.f};
        float sumAttenuversion[16] = {1.f};
        float runClickFilterGain;
        if (runSilencer)
            runClickFilterGain = runClickFilter.process(args.sampleTime, running);
        else
            runClickFilterGain = 1.f;
        for (int c = 0; c < channels; c++) {
            if (optionInput.mode[OptionInput::MOD_ATTEN])
                modAttenuversion[c] = clamp(optionInput.voltage[OptionInput::MOD_ATTEN][c] / 5.f, -1.f, 1.f);
            if (optionInput.mode[OptionInput::OP_ATTEN])
                opAttenuversion[c] = clamp(optionInput.voltage[OptionInput::OP_ATTEN][c] / 5.f, -1.f, 1.f);
            if (optionInput.mode[OptionInput::SUM_ATTEN])
                sumAttenuversion[c] = clamp(optionInput.voltage[OptionInput::SUM_ATTEN][c] / 5.f, -1.f, 1.f);
            //Note: clickGain[][][][] and clickFilters[][][][] do not convert index j with threeToFour[][], because j = 3 is used for the sum output
            if (ringMorph) {
                for (int i = 0; i < 4; i++) {
                    if (inputs[OPERATOR_INPUTS + i].isConnected()) {
                        in[c] = inputs[OPERATOR_INPUTS + i].getPolyVoltage(c) * opAttenuversion[c];
                        if (!optionInput.mode[OptionInput::SHADOW + i]) {
                            //Check current algorithm and morph target
                            if (horizontalAllowed) {
                                float ringHorizontalConnection = horizontalDestinations[backwardMorphScene[c]][i] * relativeMorphMagnitude[c];
                                clickGain[1][i][3][c] = clickFilterEnabled ? clickFilters[1][i][3][c].process(args.sampleTime, ringHorizontalConnection) : ringHorizontalConnection;
                                float horizontalConnectionA = horizontalDestinations[centerMorphScene[c]][i] * (1.f - relativeMorphMagnitude[c]);
                                float horizontalConnectionB = horizontalDestinations[forwardMorphScene[c]][i] * (relativeMorphMagnitude[c]);
                                float morphedHorizontalConnection = horizontalConnectionA + horizontalConnectionB;
                                clickGain[0][i][3][c] = clickFilterEnabled ? clickFilters[0][i][3][c].process(args.sampleTime, morphedHorizontalConnection) : morphedHorizontalConnection;
                                modOut[i][c] += (in[c] * clickGain[0][i][3][c] - in[c] * clickGain[1][i][3][c]) * modAttenuversion[c] * runClickFilterGain;
                                for (int j = 0; j < 3; j++) {
                                    float ringConnection = opDestinations[backwardMorphScene[c]][i][j] * relativeMorphMagnitude[c];
                                    clickGain[1][i][j][c] = clickFilterEnabled ? clickFilters[1][i][j][c].process(args.sampleTime, ringConnection) : ringConnection; 
                                    float connectionA = opDestinations[centerMorphScene[c]][i][j]   * (1.f - relativeMorphMagnitude[c]);
                                    float connectionB = opDestinations[forwardMorphScene[c]][i][j]  * relativeMorphMagnitude[c];
                                    float morphedConnection = connectionA + connectionB;
                                    clickGain[0][i][j][c] = clickFilterEnabled ? clickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    modOut[threeToFour[i][j]][c] += (in[c] * clickGain[0][i][j][c] - in[c] * clickGain[1][i][j][c]) * modAttenuversion[c] * runClickFilterGain;
                                }
                                carrier[backwardMorphScene[c]][i] = forcedCarrier[backwardMorphScene[c]][i];
                                float ringSumConnection = carrier[backwardMorphScene[c]][i] * relativeMorphMagnitude[c];
                                clickGain[1][i][3][c] = clickFilterEnabled ? clickFilters[1][i][3][c].process(args.sampleTime, ringSumConnection) : ringSumConnection;
                                carrier[centerMorphScene[c]][i] = forcedCarrier[centerMorphScene[c]][i];
                                carrier[forwardMorphScene[c]][i] = forcedCarrier[forwardMorphScene[c]][i];
                                float sumConnection =   carrier[centerMorphScene[c]][i]     * (1.f - relativeMorphMagnitude[c])
                                                    +   carrier[forwardMorphScene[c]][i]    * relativeMorphMagnitude[c];
                                clickGain[0][i][3][c] = clickFilterEnabled ? clickFilters[0][i][3][c].process(args.sampleTime, sumConnection) : sumConnection;
                                sumOut[c] += (in[c] * clickGain[0][i][3][c] - in[c] * clickGain[1][i][3][c]) * sumAttenuversion[c] * runClickFilterGain;
                            }
                            else {
                                for (int j = 0; j < 3; j++) {
                                    float ringConnection = opDestinations[backwardMorphScene[c]][i][j] * relativeMorphMagnitude[c] * !horizontalDestinations[backwardMorphScene[c]][i];
                                    carrier[backwardMorphScene[c]][i] = ringConnection == 0.f && !horizontalDestinations[backwardMorphScene[c]][i] ? carrier[backwardMorphScene[c]][i] : false;
                                    clickGain[1][i][j][c] = clickFilterEnabled ? clickFilters[1][i][j][c].process(args.sampleTime, ringConnection) : ringConnection; 
                                    float connectionA = opDestinations[centerMorphScene[c]][i][j]   * (1.f - relativeMorphMagnitude[c])  * !horizontalDestinations[centerMorphScene[c]][i];
                                    float connectionB = opDestinations[forwardMorphScene[c]][i][j]  * relativeMorphMagnitude[c]          * !horizontalDestinations[forwardMorphScene[c]][i];
                                    float morphedConnection = connectionA + connectionB;
                                    carrier[centerMorphScene[c]][i]  = connectionA > 0.f ? false : carrier[centerMorphScene[c]][i];
                                    carrier[forwardMorphScene[c]][i] = connectionB > 0.f ? false : carrier[forwardMorphScene[c]][i];
                                    clickGain[0][i][j][c] = clickFilterEnabled ? clickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    modOut[threeToFour[i][j]][c] += (in[c] * clickGain[0][i][j][c] - in[c] * clickGain[1][i][j][c]) * modAttenuversion[c] * runClickFilterGain;
                                }
                                carrier[backwardMorphScene[c]][i] |= forcedCarrier[backwardMorphScene[c]][i];
                                float ringSumConnection = carrier[backwardMorphScene[c]][i] * relativeMorphMagnitude[c] * !horizontalDestinations[backwardMorphScene[c]][i];
                                clickGain[1][i][3][c] = clickFilterEnabled ? clickFilters[1][i][3][c].process(args.sampleTime, ringSumConnection) : ringSumConnection;
                                carrier[centerMorphScene[c]][i] |= forcedCarrier[centerMorphScene[c]][i];
                                carrier[forwardMorphScene[c]][i] |= forcedCarrier[forwardMorphScene[c]][i];
                                float sumConnection =   carrier[centerMorphScene[c]][i]     * (1.f - relativeMorphMagnitude[c])  * !horizontalDestinations[centerMorphScene[c]][i]
                                                    +   carrier[forwardMorphScene[c]][i]    * relativeMorphMagnitude[c]          * !horizontalDestinations[forwardMorphScene[c]][i];
                                clickGain[0][i][3][c] = clickFilterEnabled ? clickFilters[0][i][3][c].process(args.sampleTime, sumConnection) : sumConnection;
                                sumOut[c] += (in[c] * clickGain[0][i][3][c] - in[c] * clickGain[1][i][3][c]) * sumAttenuversion[c] * runClickFilterGain;
                            }
                        }
                        else {
                            shadowOp[c] = optionInput.voltage[static_cast<OptionInput::Modes>(OptionInput::SHADOW + i)][c];
                            //Check current algorithm and morph target
                            if (horizontalAllowed) {
                                float ringHorizontalConnection = horizontalDestinations[backwardMorphScene[c]][i] * relativeMorphMagnitude[c];
                                clickGain[1][i][3][c] = clickFilterEnabled ? clickFilters[1][i][3][c].process(args.sampleTime, ringHorizontalConnection) : ringHorizontalConnection;
                                optionInput.shadowOpClickGains[1][i][3][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[1][i][3][c].process(args.sampleTime, ringHorizontalConnection) : ringHorizontalConnection;
                                float horizontalConnectionA = horizontalDestinations[centerMorphScene[c]][i] * (1.f - relativeMorphMagnitude[c]);
                                float horizontalConnectionB = horizontalDestinations[forwardMorphScene[c]][i] * (relativeMorphMagnitude[c]);
                                float morphedHorizontalConnection = horizontalConnectionA + horizontalConnectionB;
                                clickGain[0][i][3][c] = clickFilterEnabled ? clickFilters[0][i][3][c].process(args.sampleTime, morphedHorizontalConnection) : morphedHorizontalConnection;
                                optionInput.shadowOpClickGains[0][i][3][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][3][c].process(args.sampleTime, morphedHorizontalConnection) : morphedHorizontalConnection;
                                modOut[i][c] += ((in[c] * clickGain[0][i][3][c] - in[c] * clickGain[1][i][3][c]) + (shadowOp[c] * optionInput.shadowOpClickGains[0][i][3][c] - shadowOp[c] * optionInput.shadowOpClickGains[1][i][3][c])) * modAttenuversion[c] * runClickFilterGain;
                                for (int j = 0; j < 3; j++) {
                                    float ringConnection = opDestinations[backwardMorphScene[c]][i][j] * relativeMorphMagnitude[c];
                                    clickGain[1][i][j][c] = clickFilterEnabled ? clickFilters[1][i][j][c].process(args.sampleTime, ringConnection) : ringConnection; 
                                    optionInput.shadowOpClickGains[1][i][j][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[1][i][j][c].process(args.sampleTime, ringConnection) : ringConnection;
                                    float connectionA = opDestinations[centerMorphScene[c]][i][j]   * (1.f - relativeMorphMagnitude[c]);
                                    float connectionB = opDestinations[forwardMorphScene[c]][i][j]  * relativeMorphMagnitude[c];
                                    float morphedConnection = connectionA + connectionB;
                                    clickGain[0][i][j][c] = clickFilterEnabled ? clickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    optionInput.shadowOpClickGains[0][i][j][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    modOut[threeToFour[i][j]][c] += ((in[c] * clickGain[0][i][j][c] - in[c] * clickGain[1][i][j][c]) + (shadowOp[c] * optionInput.shadowOpClickGains[0][i][j][c] - shadowOp[c] * optionInput.shadowOpClickGains[1][i][j][c])) * modAttenuversion[c] * runClickFilterGain;
                                }
                                carrier[backwardMorphScene[c]][i] = forcedCarrier[backwardMorphScene[c]][i];
                                float ringSumConnection = carrier[backwardMorphScene[c]][i] * relativeMorphMagnitude[c];
                                clickGain[1][i][3][c] = clickFilterEnabled ? clickFilters[1][i][3][c].process(args.sampleTime, ringSumConnection) : ringSumConnection;
                                optionInput.shadowOpClickGains[1][i][3][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[1][i][3][c].process(args.sampleTime, ringSumConnection) : ringSumConnection;
                                carrier[centerMorphScene[c]][i] = forcedCarrier[centerMorphScene[c]][i];
                                carrier[forwardMorphScene[c]][i] = forcedCarrier[forwardMorphScene[c]][i];
                                float sumConnection =   carrier[centerMorphScene[c]][i]     * (1.f - relativeMorphMagnitude[c])
                                                    +   carrier[forwardMorphScene[c]][i]    * relativeMorphMagnitude[c];
                                clickGain[0][i][3][c] = clickFilterEnabled ? clickFilters[0][i][3][c].process(args.sampleTime, sumConnection) : sumConnection;
                                optionInput.shadowOpClickGains[0][i][3][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][3][c].process(args.sampleTime, sumConnection) : sumConnection;
                                sumOut[c] += ((in[c] * clickGain[0][i][3][c] - in[c] * clickGain[1][i][3][c]) + (shadowOp[c] * optionInput.shadowOpClickGains[0][i][3][c] - shadowOp[c] * optionInput.shadowOpClickGains[1][i][3][c])) * sumAttenuversion[c] * runClickFilterGain;
                            }
                            else {
                                for (int j = 0; j < 3; j++) {
                                    float ringConnection = opDestinations[backwardMorphScene[c]][i][j] * relativeMorphMagnitude[c] * !horizontalDestinations[backwardMorphScene[c]][i];
                                    clickGain[1][i][j][c] = clickFilterEnabled ? clickFilters[1][i][j][c].process(args.sampleTime, ringConnection) : ringConnection; 
                                    carrier[backwardMorphScene[c]][i] = ringConnection == 0.f && !horizontalDestinations[backwardMorphScene[c]][i] ? carrier[backwardMorphScene[c]][i] : false;
                                    optionInput.shadowOpClickGains[1][i][j][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[1][i][j][c].process(args.sampleTime, ringConnection) : ringConnection;
                                    float connectionA = opDestinations[centerMorphScene[c]][i][j]   * (1.f - relativeMorphMagnitude[c])  * !horizontalDestinations[centerMorphScene[c]][i];
                                    float connectionB = opDestinations[forwardMorphScene[c]][i][j]  * relativeMorphMagnitude[c]          * !horizontalDestinations[forwardMorphScene[c]][i];
                                    float morphedConnection = connectionA + connectionB;
                                    clickGain[0][i][j][c] = clickFilterEnabled ? clickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    carrier[centerMorphScene[c]][i]  = connectionA > 0.f ? false : carrier[centerMorphScene[c]][i];
                                    carrier[forwardMorphScene[c]][i] = connectionB > 0.f ? false : carrier[forwardMorphScene[c]][i];
                                    optionInput.shadowOpClickGains[0][i][j][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    modOut[threeToFour[i][j]][c] += ((in[c] * clickGain[0][i][j][c] - in[c] * clickGain[1][i][j][c]) + (shadowOp[c] * optionInput.shadowOpClickGains[0][i][j][c] - shadowOp[c] * optionInput.shadowOpClickGains[1][i][j][c])) * modAttenuversion[c] * runClickFilterGain;
                                }
                                carrier[backwardMorphScene[c]][i] |= forcedCarrier[backwardMorphScene[c]][i];
                                float ringSumConnection = carrier[backwardMorphScene[c]][i] * relativeMorphMagnitude[c] * !horizontalDestinations[backwardMorphScene[c]][i];
                                clickGain[1][i][3][c] = clickFilterEnabled ? clickFilters[1][i][3][c].process(args.sampleTime, ringSumConnection) : ringSumConnection;
                                optionInput.shadowOpClickGains[1][i][3][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[1][i][3][c].process(args.sampleTime, ringSumConnection) : ringSumConnection;
                                carrier[centerMorphScene[c]][i] |= forcedCarrier[centerMorphScene[c]][i];
                                carrier[forwardMorphScene[c]][i] |= forcedCarrier[forwardMorphScene[c]][i];
                                float sumConnection =   carrier[centerMorphScene[c]][i]     * (1.f - relativeMorphMagnitude[c])  * !horizontalDestinations[centerMorphScene[c]][i]
                                                    +   carrier[forwardMorphScene[c]][i]    * relativeMorphMagnitude[c]          * !horizontalDestinations[forwardMorphScene[c]][i];
                                clickGain[0][i][3][c] = clickFilterEnabled ? clickFilters[0][i][3][c].process(args.sampleTime, sumConnection) : sumConnection;
                                optionInput.shadowOpClickGains[0][i][3][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][3][c].process(args.sampleTime, sumConnection) : sumConnection;
                                sumOut[c] += ((in[c] * clickGain[0][i][3][c] - in[c] * clickGain[1][i][3][c]) + (shadowOp[c] * optionInput.shadowOpClickGains[0][i][3][c] - shadowOp[c] * optionInput.shadowOpClickGains[1][i][3][c])) * sumAttenuversion[c] * runClickFilterGain;
                            }
                        }
                    }
                }
            }
            else {
                for (int i = 0; i < 4; i++) {
                    if (inputs[OPERATOR_INPUTS + i].isConnected()) {
                        in[c] = inputs[OPERATOR_INPUTS + i].getPolyVoltage(c) * opAttenuversion[c];
                        if (!optionInput.mode[OptionInput::SHADOW + i]) {
                            //Check current algorithm and morph target
                            if (horizontalAllowed) {
                                float horizontalConnectionA = horizontalDestinations[centerMorphScene[c]][i] * (1.f - relativeMorphMagnitude[c]);
                                float horizontalConnectionB = horizontalDestinations[forwardMorphScene[c]][i] * (relativeMorphMagnitude[c]);
                                float morphedHorizontalConnection = horizontalConnectionA + horizontalConnectionB;
                                clickGain[0][i][3][c] = clickFilterEnabled ? clickFilters[0][i][3][c].process(args.sampleTime, morphedHorizontalConnection) : morphedHorizontalConnection;
                                modOut[i][c] += in[c] * clickGain[0][i][3][c] * modAttenuversion[c] * runClickFilterGain;
                                for (int j = 0; j < 3; j++) {
                                    float connectionA = opDestinations[centerMorphScene[c]][i][j]   * (1.f - relativeMorphMagnitude[c]);
                                    float connectionB = opDestinations[forwardMorphScene[c]][i][j]  * relativeMorphMagnitude[c];
                                    float morphedConnection = connectionA + connectionB;
                                    clickGain[0][i][j][c] = clickFilterEnabled ? clickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    modOut[threeToFour[i][j]][c] += in[c] * clickGain[0][i][j][c]  * modAttenuversion[c] * runClickFilterGain;
                                }
                                carrier[centerMorphScene[c]][i] = forcedCarrier[centerMorphScene[c]][i];
                                carrier[forwardMorphScene[c]][i] = forcedCarrier[forwardMorphScene[c]][i];
                                float sumConnection =   carrier[centerMorphScene[c]][i]     * (1.f - relativeMorphMagnitude[c])
                                                    +   carrier[forwardMorphScene[c]][i]    * relativeMorphMagnitude[c];
                                clickGain[0][i][4][c] = clickFilterEnabled ? clickFilters[0][i][4][c].process(args.sampleTime, sumConnection) : sumConnection;
                                sumOut[c] += in[c] * clickGain[0][i][4][c] * sumAttenuversion[c] * runClickFilterGain;
                            }
                            else {
                                for (int j = 0; j < 3; j++) {
                                    float connectionA = opDestinations[centerMorphScene[c]][i][j]   * (1.f - relativeMorphMagnitude[c])  * !horizontalDestinations[centerMorphScene[c]][i];
                                    float connectionB = opDestinations[forwardMorphScene[c]][i][j]  * relativeMorphMagnitude[c]          * !horizontalDestinations[forwardMorphScene[c]][i];
                                    float morphedConnection = connectionA + connectionB;
                                    clickGain[0][i][j][c] = clickFilterEnabled ? clickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    carrier[centerMorphScene[c]][i]  = connectionA > 0.f ? false : carrier[centerMorphScene[c]][i];
                                    carrier[forwardMorphScene[c]][i] = connectionB > 0.f ? false : carrier[forwardMorphScene[c]][i];
                                    modOut[threeToFour[i][j]][c] += in[c] * clickGain[0][i][j][c]  * modAttenuversion[c] * runClickFilterGain;
                                }
                                carrier[centerMorphScene[c]][i] |= forcedCarrier[centerMorphScene[c]][i];
                                carrier[forwardMorphScene[c]][i] |= forcedCarrier[forwardMorphScene[c]][i];
                                float sumConnection =   carrier[centerMorphScene[c]][i]     * (1.f - relativeMorphMagnitude[c])  * !horizontalDestinations[centerMorphScene[c]][i]
                                                    +   carrier[forwardMorphScene[c]][i]    * relativeMorphMagnitude[c]          * !horizontalDestinations[forwardMorphScene[c]][i];
                                clickGain[0][i][4][c] = clickFilterEnabled ? clickFilters[0][i][4][c].process(args.sampleTime, sumConnection) : sumConnection;
                                sumOut[c] += in[c] * clickGain[0][i][4][c] * sumAttenuversion[c] * runClickFilterGain;
                            }
                        }
                        else {
                            shadowOp[c] = optionInput.voltage[static_cast<OptionInput::Modes>(OptionInput::SHADOW + i)][c];
                            //Check current algorithm and morph target
                            if (horizontalAllowed) {
                                float horizontalConnectionA = horizontalDestinations[centerMorphScene[c]][i] * (1.f - relativeMorphMagnitude[c]);
                                float horizontalConnectionB = horizontalDestinations[forwardMorphScene[c]][i] * (relativeMorphMagnitude[c]);
                                float morphedHorizontalConnection = horizontalConnectionA + horizontalConnectionB;
                                clickGain[0][i][3][c] = clickFilterEnabled ? clickFilters[0][i][3][c].process(args.sampleTime, morphedHorizontalConnection) : morphedHorizontalConnection;
                                optionInput.shadowOpClickGains[0][i][3][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][3][c].process(args.sampleTime, morphedHorizontalConnection) : morphedHorizontalConnection;
                                modOut[i][c] += (in[c] * clickGain[0][i][3][c] + shadowOp[c] * optionInput.shadowOpClickGains[0][i][3][c]) * modAttenuversion[c] * runClickFilterGain;
                                for (int j = 0; j < 3; j++) {
                                    float connectionA = opDestinations[centerMorphScene[c]][i][j]   * (1.f - relativeMorphMagnitude[c]);
                                    float connectionB = opDestinations[forwardMorphScene[c]][i][j]  * relativeMorphMagnitude[c];
                                    float morphedConnection = connectionA + connectionB;
                                    clickGain[0][i][j][c] = clickFilterEnabled ? clickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    optionInput.shadowOpClickGains[0][i][j][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    modOut[threeToFour[i][j]][c] += (in[c] * clickGain[0][i][j][c] + shadowOp[c] * optionInput.shadowOpClickGains[0][i][j][c]) * modAttenuversion[c] * runClickFilterGain;
                                }
                                carrier[centerMorphScene[c]][i] = forcedCarrier[centerMorphScene[c]][i];
                                carrier[forwardMorphScene[c]][i] = forcedCarrier[forwardMorphScene[c]][i];
                                float sumConnection =   carrier[centerMorphScene[c]][i]     * (1.f - relativeMorphMagnitude[c])
                                                    +   carrier[forwardMorphScene[c]][i]    * relativeMorphMagnitude[c];
                                clickGain[0][i][4][c] = clickFilterEnabled ? clickFilters[0][i][4][c].process(args.sampleTime, sumConnection) : sumConnection;
                                optionInput.shadowOpClickGains[0][i][4][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][4][c].process(args.sampleTime, sumConnection) : sumConnection;
                                sumOut[c] += (in[c] * clickGain[0][i][4][c] + shadowOp[c] * optionInput.shadowOpClickGains[0][i][4][c]) * sumAttenuversion[c] * runClickFilterGain;
                            }
                            else {
                                for (int j = 0; j < 3; j++) {
                                    float connectionA = opDestinations[centerMorphScene[c]][i][j]   * (1.f - relativeMorphMagnitude[c])  * !horizontalDestinations[centerMorphScene[c]][i];
                                    float connectionB = opDestinations[forwardMorphScene[c]][i][j]  * relativeMorphMagnitude[c]          * !horizontalDestinations[forwardMorphScene[c]][i];
                                    float morphedConnection = connectionA + connectionB;
                                    clickGain[0][i][j][c] = clickFilterEnabled ? clickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    carrier[centerMorphScene[c]][i]  = connectionA > 0.f ? false : carrier[centerMorphScene[c]][i];
                                    carrier[forwardMorphScene[c]][i] = connectionB > 0.f ? false : carrier[forwardMorphScene[c]][i];
                                    optionInput.shadowOpClickGains[0][i][j][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][j][c].process(args.sampleTime, morphedConnection) : morphedConnection;
                                    modOut[threeToFour[i][j]][c] += (in[c] * clickGain[0][i][j][c] + shadowOp[c] * optionInput.shadowOpClickGains[0][i][j][c]) * modAttenuversion[c] * runClickFilterGain;
                                }
                                carrier[centerMorphScene[c]][i] |= forcedCarrier[centerMorphScene[c]][i];
                                carrier[forwardMorphScene[c]][i] |= forcedCarrier[forwardMorphScene[c]][i];
                                float sumConnection =   carrier[centerMorphScene[c]][i]     * (1.f - relativeMorphMagnitude[c])  * !horizontalDestinations[centerMorphScene[c]][i]
                                                    +   carrier[forwardMorphScene[c]][i]    * relativeMorphMagnitude[c]          * !horizontalDestinations[forwardMorphScene[c]][i];
                                clickGain[0][i][4][c] = clickFilterEnabled ? clickFilters[0][i][4][c].process(args.sampleTime, sumConnection) : sumConnection;
                                optionInput.shadowOpClickGains[0][i][4][c] = clickFilterEnabled ? optionInput.shadowOpClickFilters[0][i][4][c].process(args.sampleTime, sumConnection) : sumConnection;
                                sumOut[c] += (in[c] * clickGain[0][i][4][c] + shadowOp[c] * optionInput.shadowOpClickGains[0][i][4][c]) * sumAttenuversion[c] * runClickFilterGain;
                            }
                        }
                    }
                }
            }
            if (optionInput.mode[OptionInput::WILDCARD_MOD]) {
                wildcardMod[c] = optionInput.voltage[OptionInput::WILDCARD_MOD][c];
                optionInput.wildcardModClickGain = (clickFilterEnabled ? optionInput.wildcardModClickFilter[c].process(args.sampleTime, optionInput.mode[OptionInput::WILDCARD_MOD]) : optionInput.mode[OptionInput::WILDCARD_MOD]);
                for (int i = 0; i < 4; i++) {
                    modOut[i][c] += wildcardMod[c] * optionInput.wildcardModClickGain * modAttenuversion[c] * runClickFilterGain;

                }
            }
            if (optionInput.mode[OptionInput::WILDCARD_SUM]) {
                wildcardSum[c] = optionInput.voltage[OptionInput::WILDCARD_SUM][c];
                optionInput.wildcardSumClickGain = (clickFilterEnabled ? optionInput.wildcardSumClickFilter[c].process(args.sampleTime, optionInput.mode[OptionInput::WILDCARD_SUM]) : optionInput.mode[OptionInput::WILDCARD_SUM]);
                sumOut[c] += wildcardSum[c] * optionInput.wildcardSumClickGain * sumAttenuversion[c] * runClickFilterGain;
            }
            //Grab values from Option Input
        }

        //Set outputs
        for (int i = 0; i < 4; i++) {
            if (outputs[MODULATOR_OUTPUTS + i].isConnected()) {
                outputs[MODULATOR_OUTPUTS + i].setChannels(channels);
                outputs[MODULATOR_OUTPUTS + i].writeVoltages(modOut[i]);
            }
        }
        if (outputs[SUM_OUTPUT].isConnected()) {
            outputs[SUM_OUTPUT].setChannels(channels);
            outputs[SUM_OUTPUT].writeVoltages(sumOut);
        }

        //Set lights
        if (lightDivider.process()) {
            if (configMode) {   //Display state without morph, highlight configScene
                //Set backlight
                //Set purple component to off
                lights[DISPLAY_BACKLIGHT].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                //Set yellow component
                lights[DISPLAY_BACKLIGHT + 1].setSmoothBrightness(getPortBrightness(outputs[SUM_OUTPUT]) / 2048.f + 0.014325f + clamp(optionInput.voltage[OptionInput::SCREEN_BRIGHTNESS][0], 0.f, 10.f) / 1536.f, args.sampleTime * lightDivider.getDivision());
                //Set edit light
                lights[EDIT_LIGHT].setSmoothBrightness(1.f, args.sampleTime * lightDivider.getDivision());
                //Set scene lights
                for (int i = 0; i < 3; i++) {
                    //Set purple component to off
                    lights[SCENE_LIGHTS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                    //Set yellow component depending on config scene and blink status
                    lights[SCENE_LIGHTS + i * 3 + 1].setSmoothBrightness(configScene == i ? blinkStatus : 0.f, args.sampleTime * lightDivider.getDivision());
                }
                //Set op/mod lights
                float ringBrightnessInput = 0.f;
                if (optionInput.mode[OptionInput::CONNECTION_BRIGHTNESS])
                    ringBrightnessInput = clamp(optionInput.voltage[OptionInput::CONNECTION_BRIGHTNESS][0], 0.f, 10.f) / 8.f;
                for (int i = 0; i < 4; i++) {
                    if (horizontalDestinations[configScene][i]) {
                        if (horizontalAllowed) {
                            //Set carrier indicator
                            if (forcedCarrier[configScene][i]) {
                                //Purple light
                                lights[CARRIER_INDICATORS + i * 3].setSmoothBrightness(configOp == i ?
                                    blinkStatus ?
                                        0.f
                                        : INDICATOR_BRIGHTNESS
                                    : INDICATOR_BRIGHTNESS, args.sampleTime * lightDivider.getDivision());
                                //Yellow light
                                lights[CARRIER_INDICATORS + i * 3 + 1].setSmoothBrightness(configOp == i ?
                                    blinkStatus
                                    : 0.f, args.sampleTime * lightDivider.getDivision());
                                //Red light
                                lights[CARRIER_INDICATORS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                            }
                            else {
                                //Purple light
                                lights[CARRIER_INDICATORS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                                //Yellow light
                                lights[CARRIER_INDICATORS + i * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                                //Red light
                                lights[CARRIER_INDICATORS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                            }
                            //Set op lights
                            //Purple lights
                            lights[OPERATOR_LIGHTS + i * 3].setSmoothBrightness(configOp == i ?
                                blinkStatus ?
                                    0.f
                                    : DEF_RED_BRIGHTNESS
                                : DEF_RED_BRIGHTNESS, args.sampleTime * lightDivider.getDivision());
                            //Yellow Lights
                            lights[OPERATOR_LIGHTS + i * 3 + 1].setSmoothBrightness(configOp == i ?
                                blinkStatus
                                : 0.f, args.sampleTime * lightDivider.getDivision());
                            //Red lights
                            lights[OPERATOR_LIGHTS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        }
                        else {
                            //Set carrier indicator
                            if (forcedCarrier[configScene][i]) {
                                //Purple light
                                lights[CARRIER_INDICATORS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                                //Yellow light
                                lights[CARRIER_INDICATORS + i * 3 + 1].setSmoothBrightness(configOp == i ?
                                    blinkStatus
                                    : 0.f, args.sampleTime * lightDivider.getDivision());
                                //Red light
                                lights[CARRIER_INDICATORS + i * 3 + 2].setSmoothBrightness(configOp == i ?
                                    blinkStatus ?
                                        0.f
                                        : DEF_RED_BRIGHTNESS
                                    : DEF_RED_BRIGHTNESS, args.sampleTime * lightDivider.getDivision());
                            }
                            else {
                                //Purple light
                                lights[CARRIER_INDICATORS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                                //Yellow light
                                lights[CARRIER_INDICATORS + i * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                                //Red light
                                lights[CARRIER_INDICATORS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                            }
                            //Set op lights
                            //Purple lights
                            lights[OPERATOR_LIGHTS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                            //Yellow Lights
                            lights[OPERATOR_LIGHTS + i * 3 + 1].setSmoothBrightness(configOp == i ?
                                blinkStatus
                                : 0.f, args.sampleTime * lightDivider.getDivision());
                            //Red lights
                            lights[OPERATOR_LIGHTS + i * 3 + 2].setSmoothBrightness(configOp == i ?
                                blinkStatus ?
                                    0.f
                                    : DEF_RED_BRIGHTNESS
                                : DEF_RED_BRIGHTNESS, args.sampleTime * lightDivider.getDivision());
                        }
                    }
                    else {
                        //Set carrier indicator
                        if (forcedCarrier[configScene][i]) {
                            //Purple light
                            lights[CARRIER_INDICATORS + i * 3].setSmoothBrightness(configOp == i ?
                                blinkStatus ?
                                    0.f
                                    : INDICATOR_BRIGHTNESS
                                : INDICATOR_BRIGHTNESS, args.sampleTime * lightDivider.getDivision());
                            //Yellow light
                            lights[CARRIER_INDICATORS + i * 3 + 1].setSmoothBrightness(configOp == i ?
                                blinkStatus
                                : 0.f, args.sampleTime * lightDivider.getDivision());
                            //Red light
                            lights[CARRIER_INDICATORS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        }
                        else {
                            //Purple light
                            lights[CARRIER_INDICATORS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                            //Yellow light
                            lights[CARRIER_INDICATORS + i * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                            //Red light
                            lights[CARRIER_INDICATORS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        }
                        //Set op lights
                        //Purple lights
                        lights[OPERATOR_LIGHTS + i * 3].setSmoothBrightness(configOp == i ?
                            blinkStatus ?
                                0.f
                                : getPortBrightness(inputs[OPERATOR_INPUTS + i]) + ringBrightnessInput
                            : getPortBrightness(inputs[OPERATOR_INPUTS + i]) + ringBrightnessInput, args.sampleTime * lightDivider.getDivision());
                        //Yellow Lights
                        lights[OPERATOR_LIGHTS + i * 3 + 1].setSmoothBrightness(configOp == i ? blinkStatus : 0.f, args.sampleTime * lightDivider.getDivision());
                        //Red lights
                        lights[OPERATOR_LIGHTS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                    }
                    //Set mod lights
                    if (i != configOp) {
                        //Purple lights
                        lights[MODULATOR_LIGHTS + i * 3].setSmoothBrightness(configOp > -1 ?
                            opDestinations[configScene][configOp][i < configOp ? i : i - 1] ?
                                blinkStatus ?
                                    0.f
                                    : getPortBrightness(outputs[MODULATOR_OUTPUTS + i]) + ringBrightnessInput
                                : getPortBrightness(outputs[MODULATOR_OUTPUTS + i]) + ringBrightnessInput
                            : getPortBrightness(outputs[MODULATOR_OUTPUTS + i]) + ringBrightnessInput, args.sampleTime * lightDivider.getDivision());
                        //Yellow lights
                        lights[MODULATOR_LIGHTS + i * 3 + 1].setSmoothBrightness(configOp > -1 ?
                            (opDestinations[configScene][configOp][i < configOp ? i : i - 1] ?
                                blinkStatus
                                : 0.f)
                            : 0.f, args.sampleTime * lightDivider.getDivision());
                    }
                    else {
                        if (horizontalAllowed) {
                            //Purple lights
                            lights[MODULATOR_LIGHTS + i * 3].setSmoothBrightness(getPortBrightness(outputs[MODULATOR_OUTPUTS + i]) + ringBrightnessInput, args.sampleTime * lightDivider.getDivision());
                            //Yellow lights
                            lights[MODULATOR_LIGHTS + i * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        }
                        else {
                            //Purple lights
                            lights[MODULATOR_LIGHTS + i * 3].setSmoothBrightness(configOp > -1 ?
                                !horizontalDestinations[configScene][configOp] ?
                                    getPortBrightness(outputs[MODULATOR_OUTPUTS + i]) + ringBrightnessInput
                                    : blinkStatus ?
                                        0.f
                                        : getPortBrightness(outputs[MODULATOR_OUTPUTS + i]) + ringBrightnessInput
                                : getPortBrightness(outputs[MODULATOR_OUTPUTS + i]) + ringBrightnessInput, args.sampleTime * lightDivider.getDivision());
                            //Yellow lights
                            lights[MODULATOR_LIGHTS + i * 3 + 1].setSmoothBrightness(configOp > -1 ?
                                !horizontalDestinations[configScene][configOp] ?
                                    0.f
                                    : blinkStatus
                                : 0.f, args.sampleTime * lightDivider.getDivision());
                        }
                    }
                }
                //Check and update blink timer
                if (blinkTimer > BLINK_INTERVAL / lightDivider.getDivision()) {
                    blinkStatus ^= true;
                    blinkTimer = 0.f;
                }
                else
                    blinkTimer += args.sampleTime;
                //Set connection lights
                if (horizontalAllowed) {
                    for (int i = 0; i < 4; i++) {
                        //Purple lights
                        lights[H_CONNECTION_LIGHTS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Yellow lights
                        lights[H_CONNECTION_LIGHTS + i * 3 + 1].setSmoothBrightness(horizontalDestinations[configScene][i] ?
                            0.4f
                            : 0.f, args.sampleTime * lightDivider.getDivision());
                        //Red lights
                        lights[H_CONNECTION_LIGHTS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                    }
                    for (int i = 0; i < 12; i++) {
                        //Purple lights
                        lights[CONNECTION_LIGHTS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Yellow lights
                        lights[CONNECTION_LIGHTS + i * 3 + 1].setSmoothBrightness(opDestinations[configScene][i / 3][i % 3] ?
                            0.4f
                            : 0.f, args.sampleTime * lightDivider.getDivision());
                        //Set diagonal disable lights
                        lights[D_DISABLE_LIGHTS + i].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                    }
                }
                else {
                    for (int i = 0; i < 12; i++) {
                        //Purple lights
                        lights[CONNECTION_LIGHTS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Yellow lights
                        lights[CONNECTION_LIGHTS + i * 3 + 1].setSmoothBrightness(!horizontalDestinations[configScene][i / 3] ? 
                            opDestinations[configScene][i / 3][i % 3] ?
                                0.4f
                                : 0.f
                            : 0.f, args.sampleTime * lightDivider.getDivision());
                    }
                    //Set horizontal lights
                    for (int i = 0; i < 4; i++) {
                        //Purple lights
                        lights[H_CONNECTION_LIGHTS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Yellow lights
                        lights[H_CONNECTION_LIGHTS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Red lights
                        lights[H_CONNECTION_LIGHTS + i * 3 + 2].setSmoothBrightness(!horizontalDestinations[configScene][i] ?
                            0.f
                            : DEF_RED_BRIGHTNESS, args.sampleTime * lightDivider.getDivision());
                    }
                    //Set diagonal disable lights
                    for (int i = 0; i < 12; i++)
                        lights[D_DISABLE_LIGHTS + i].setSmoothBrightness(!horizontalDestinations[configScene][i / 3] ? 
                            0.f
                            : opDestinations[configScene][i / 3][i % 3] ?
                                DEF_RED_BRIGHTNESS
                                : 0.f, args.sampleTime * lightDivider.getDivision());
                }
            } 
            else {
                //Set backlight
                float screenBrightnessInput = 0.f;
                if (optionInput.mode[OptionInput::SCREEN_BRIGHTNESS])
                    screenBrightnessInput = clamp(optionInput.voltage[OptionInput::SCREEN_BRIGHTNESS][0], 0.f, 10.f) / 768.f;
                //Set yellow component to off
                lights[DISPLAY_BACKLIGHT + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                //Set purple component
                lights[DISPLAY_BACKLIGHT].setSmoothBrightness(getPortBrightness(outputs[SUM_OUTPUT]) / 1024.f + 0.014325f + screenBrightnessInput, args.sampleTime * lightDivider.getDivision());
                //Set edit light
                lights[EDIT_LIGHT].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                //Display morphed state
                float brightness;
                //Set scene lights
                //Set base scene light
                for (int i = 0; i < 3; i++) {
                    //Set yellow component to off
                    lights[SCENE_LIGHTS + i * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                    //Set purple component depending on inverse morph
                    lights[SCENE_LIGHTS + i * 3].setSmoothBrightness(i == (baseScene + sceneOffset[0]) % 3 ? 1.f - rescale(relativeMorphMagnitude[0], 0.f, 1.f, 0.f, .635f) : 0.f, args.sampleTime * lightDivider.getDivision());
                }
                //Set morph target light's purple component depending on morph
                lights[SCENE_LIGHTS + forwardMorphScene[0] * 3].setSmoothBrightness(relativeMorphMagnitude[0], args.sampleTime * lightDivider.getDivision());
                //Set op/mod lights and carrier indicators
                updateSceneBrightnesses();
                for (int i = 0; i < 4; i++) {
                    //Purple, yellow, and red lights
                    for (int j = 0; j < 3; j++) {
                        lights[OPERATOR_LIGHTS + i * 3 + j].setSmoothBrightness(crossfade(sceneBrightnesses[centerMorphScene[0]][i][j], sceneBrightnesses[forwardMorphScene[0]][i][j], relativeMorphMagnitude[0]), args.sampleTime * lightDivider.getDivision()); 
                        lights[MODULATOR_LIGHTS + i * 3 + j].setSmoothBrightness(crossfade(sceneBrightnesses[centerMorphScene[0]][i + 4][j], sceneBrightnesses[forwardMorphScene[0]][i + 4][j], relativeMorphMagnitude[0]), args.sampleTime * lightDivider.getDivision()); 
                        lights[CARRIER_INDICATORS + i * 3 + j].setSmoothBrightness(crossfade(sceneBrightnesses[centerMorphScene[0]][i + 8][j], sceneBrightnesses[forwardMorphScene[0]][i + 8][j], relativeMorphMagnitude[0]), args.sampleTime * lightDivider.getDivision()); 
                    }
                }
                //Set connection lights
                float connectionLineBrightnessInput = 0.f;
                if (optionInput.mode[OptionInput::CONNECTION_BRIGHTNESS])
                    connectionLineBrightnessInput = clamp(optionInput.voltage[OptionInput::CONNECTION_BRIGHTNESS][0], 0.f, 10.f) / 16.f;
                if (horizontalAllowed) {
                    for (int i = 0; i < 4; i++) {
                        brightness = 0.f;
                        if (horizontalDestinations[centerMorphScene[0]][i])
                            brightness += getPortBrightness(inputs[OPERATOR_INPUTS + i]) * (1.f - relativeMorphMagnitude[0]) + connectionLineBrightnessInput;
                        if (horizontalDestinations[forwardMorphScene[0]][i])
                            brightness += getPortBrightness(inputs[OPERATOR_INPUTS + i]) * relativeMorphMagnitude[0] + connectionLineBrightnessInput;
                        //Purple lights
                        lights[H_CONNECTION_LIGHTS + i * 3].setSmoothBrightness(brightness, args.sampleTime * lightDivider.getDivision());
                        //Yellow
                        lights[H_CONNECTION_LIGHTS + i * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Red
                        lights[H_CONNECTION_LIGHTS + i * 3 + 2].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                    }
                    for (int i = 0; i < 12; i++) {
                        brightness = 0.f;
                        if (opDestinations[centerMorphScene[0]][i / 3][i % 3])
                            brightness += getPortBrightness(inputs[OPERATOR_INPUTS + i / 3]) * (1.f - relativeMorphMagnitude[0]) + connectionLineBrightnessInput;
                        if (opDestinations[forwardMorphScene[0]][i / 3][i % 3])
                            brightness += getPortBrightness(inputs[OPERATOR_INPUTS + i / 3]) * relativeMorphMagnitude[0] + connectionLineBrightnessInput;
                        //Purple lights
                        lights[CONNECTION_LIGHTS + i * 3].setSmoothBrightness(brightness, args.sampleTime * lightDivider.getDivision());
                        //Yellow
                        lights[CONNECTION_LIGHTS + i * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Set diagonal disable lights
                        lights[D_DISABLE_LIGHTS + i].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                    }
                }
                else {
                    for (int i = 0; i < 4; i++) {
                        brightness = getPortBrightness(inputs[OPERATOR_INPUTS + i]);
                        for (int j = 0; j < 3; j++) {
                            float morphBrightness = 0.f;
                            if (opDestinations[centerMorphScene[0]][i][j]) {
                                if (!horizontalDestinations[centerMorphScene[0]][i])
                                    morphBrightness += brightness * (1.f - relativeMorphMagnitude[0]) + connectionLineBrightnessInput;
                            }
                            if (opDestinations[forwardMorphScene[0]][i][j]) {
                                if (!horizontalDestinations[forwardMorphScene[0]][i])
                                    morphBrightness += brightness * relativeMorphMagnitude[0] + connectionLineBrightnessInput;
                            }
                            //Purple lights
                            lights[CONNECTION_LIGHTS + i * 9 + j * 3].setSmoothBrightness(morphBrightness, args.sampleTime * lightDivider.getDivision());
                            //Yellow
                            lights[CONNECTION_LIGHTS + i * 9 + j * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                            //Set diagonal disable lights
                            lights[D_DISABLE_LIGHTS + i * 3 + j].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        }
                        //Set horizontal disable lights
                        float horizontalMorphBrightness = 0.f;
                        if (horizontalDestinations[centerMorphScene[0]][i])
                            horizontalMorphBrightness += (1.f - relativeMorphMagnitude[0]) + connectionLineBrightnessInput;
                        if (horizontalDestinations[forwardMorphScene[0]][i])
                            horizontalMorphBrightness += relativeMorphMagnitude[0] + connectionLineBrightnessInput;
                        //Purple lights
                        lights[H_CONNECTION_LIGHTS + i * 3].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Yellow lights
                        lights[H_CONNECTION_LIGHTS + i * 3 + 1].setSmoothBrightness(0.f, args.sampleTime * lightDivider.getDivision());
                        //Red lights
                        lights[H_CONNECTION_LIGHTS + i * 3 + 2].setSmoothBrightness(horizontalMorphBrightness, args.sampleTime * lightDivider.getDivision());
                    }
                }
            }
        }

        if (clockIgnoreOnReset > 0l)
			clockIgnoreOnReset--;
    }

    void initRun() {
		clockIgnoreOnReset = (long) (CLOCK_IGNORE_DURATION * APP->engine->getSampleRate());
        baseScene = resetScene;
	}

    inline float getPortBrightness(Port port) {
        if (vuLights)
            return std::max(    {   port.plugLights[0].getBrightness(),
                                    port.plugLights[1].getBrightness() * 4,
                                    port.plugLights[2].getBrightness()          }   );
        else
            return 1.f;
    }

    void updateSceneBrightnesses() {
        float connectionBrightnessInput = 0.f;
        if (optionInput.mode[OptionInput::CONNECTION_BRIGHTNESS]) 
            connectionBrightnessInput = clamp(optionInput.voltage[OptionInput::CONNECTION_BRIGHTNESS][0], 0.f, 10.f) / 8.f;
        if (horizontalAllowed) {
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 4; j++) {
                    //Op lights
                    //Purple lights
                    sceneBrightnesses[i][j][0] = getPortBrightness(inputs[OPERATOR_INPUTS + j]) + connectionBrightnessInput;
                    //Mod lights
                    //Purple lights
                    sceneBrightnesses[i][j + 4][0] = getPortBrightness(outputs[MODULATOR_OUTPUTS + j]) + connectionBrightnessInput;
                    //Op lights
                    //Yellow Lights
                    sceneBrightnesses[i][j][1] = 0.f;
                    //Red lights
                    sceneBrightnesses[i][j][2] = 0.f;
                    //Carrier indicators
                    if (forcedCarrier[i][j]) {
                        //Purple lights
                        sceneBrightnesses[i][j + 8][0] = INDICATOR_BRIGHTNESS;
                    }
                    else {
                        //Purple lights
                        sceneBrightnesses[i][j + 8][0] = 0.f;
                    }
                    //Yellow lights
                    sceneBrightnesses[i][j + 8][1] = 0.f;
                    //Red lights
                    sceneBrightnesses[i][j + 8][2] = 0.f;
                }
            }
        }
        else {
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 4; j++) {
                    if (!horizontalDestinations[i][j]) {
                        //Op lights
                        //Purple lights
                        sceneBrightnesses[i][j][0] = getPortBrightness(inputs[OPERATOR_INPUTS + j]) + connectionBrightnessInput;
                        //Mod lights
                        //Purple lights
                        sceneBrightnesses[i][j + 4][0] = getPortBrightness(outputs[MODULATOR_OUTPUTS + j]) + connectionBrightnessInput;
                        //Carrier indicators
                        if (forcedCarrier[i][j]) {
                            //Purple lights
                            sceneBrightnesses[i][j + 8][0] = INDICATOR_BRIGHTNESS;
                        }
                        else {
                            //Purple lights
                            sceneBrightnesses[i][j + 8][0] = 0.f;
                        }
                        //Red lights
                        sceneBrightnesses[i][j + 8][2] = 0.f;
                    }
                    else {
                        //Op lights
                        //Purple lights
                        sceneBrightnesses[i][j][0] = 0.f;
                        //Carrier indicators
                        if (forcedCarrier[i][j]) {
                            //Red lights
                            sceneBrightnesses[i][j + 8][2] = INDICATOR_BRIGHTNESS;
                        }
                        else {
                            //Red lights
                            sceneBrightnesses[i][j + 8][2] = 0.f;
                        }
                        //Purple lights
                        sceneBrightnesses[i][j + 8][0] = 0.f;
                    }
                    //Op lights
                    //Yellow Lights
                    sceneBrightnesses[i][j][1] = 0.f;
                    //Red lights
                    sceneBrightnesses[i][j][2] = horizontalDestinations[i][j];
                    //Carrier indicators
                    //Yellow lights
                    sceneBrightnesses[i][j + 8][1] = 0.f;
                }
            }
        }
    }

    void swapAlgorithms(int a, int b) {
        bool swap[4][3] = {false};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 3; j++) {
                swap[i][j] = opDestinations[a][i][j];
                opDestinations[a][i][j] = opDestinations[b][i][j];
                opDestinations[b][i][j] = swap[i][j];
            }
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "Config Enabled", json_boolean(configMode));
        json_object_set_new(rootJ, "Config Mode", json_integer(configOp));
        json_object_set_new(rootJ, "Config Scene", json_integer(configOp));
        json_object_set_new(rootJ, "Current Scene", json_integer(baseScene));
        json_object_set_new(rootJ, "Horizontal Allowed", json_integer(horizontalAllowed));
        json_object_set_new(rootJ, "Reset Scene", json_integer(resetScene));
        json_object_set_new(rootJ, "Ring Morph", json_boolean(ringMorph));
        json_object_set_new(rootJ, "Randomize Ring Morph", json_boolean(randomRingMorph));
        json_object_set_new(rootJ, "Auto Exit", json_boolean(exitConfigOnConnect));
        json_object_set_new(rootJ, "CCW Scene Selection", json_boolean(ccwSceneSelection));
        json_object_set_new(rootJ, "Reset on Run", json_boolean(resetOnRun));
        json_object_set_new(rootJ, "Click Filter Enabled", json_boolean(clickFilterEnabled));
        json_object_set_new(rootJ, "Glowing Ink", json_boolean(glowingInk));
        json_object_set_new(rootJ, "VU Lights", json_boolean(vuLights));
        json_t* opInputModesJ = json_array();
        for (int i = 0; i < OptionInput::NUM_MODES; i++) {
            json_t* inputModeJ = json_object();
            json_object_set_new(inputModeJ, "Destination", json_boolean(optionInput.mode[i]));
            json_array_append_new(opInputModesJ, inputModeJ);
        }
        json_object_set_new(rootJ, "Option Input Modes", opInputModesJ);
        json_object_set_new(rootJ, "Allow Multiple Modes", json_boolean(optionInput.allowMultipleModes));
        json_object_set_new(rootJ, "Forget Option Voltage", json_boolean(optionInput.forgetVoltage));
        
        json_t* opDestinationsJ = json_array();
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                for (int k = 0; k < 3; k++) {
                    json_t* destinationJ = json_object();
                    json_object_set_new(destinationJ, "Destination", json_boolean(opDestinations[i][j][k]));
                    json_array_append_new(opDestinationsJ, destinationJ);
                }
            }
        }
        json_object_set_new(rootJ, "Operator Destinations", opDestinationsJ);

        json_t* algoNamesJ = json_array();
        for (int i = 0; i < 3; i++) {
            json_t* nameJ = json_object();
            json_object_set_new(nameJ, "Name", json_integer(algoName[i].to_ullong()));
            json_array_append_new(algoNamesJ, nameJ);
        }
        json_object_set_new(rootJ, "Algorithm Names", algoNamesJ);

        json_t* horizontalConnectionsJ = json_array();
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                json_t* connectionJ = json_object();
                json_object_set_new(connectionJ, "Enabled Op", json_boolean(!horizontalDestinations[i][j]));
                json_array_append_new(horizontalConnectionsJ, connectionJ);
            }
        }
        json_object_set_new(rootJ, "Operators Enabled", horizontalConnectionsJ);

        json_t* forcedCarriersJ = json_array();
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                json_t* forcedCarrierJ = json_object();
                json_object_set_new(forcedCarrierJ, "Forced Carrier", json_boolean(forcedCarrier[i][j]));
                json_array_append_new(forcedCarriersJ, forcedCarrierJ);
            }
        }
        json_object_set_new(rootJ, "Forced Carriers", forcedCarriersJ);

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        configMode = json_integer_value(json_object_get(rootJ, "Config Enabled"));
        configOp = json_integer_value(json_object_get(rootJ, "Config Mode"));
        configScene = json_integer_value(json_object_get(rootJ, "Config Scene"));
        baseScene = json_integer_value(json_object_get(rootJ, "Current Scene"));
        horizontalAllowed = json_integer_value(json_object_get(rootJ, "Horizontal Allowed"));
        resetScene = json_integer_value(json_object_get(rootJ, "Reset Scene"));
        ringMorph = json_boolean_value(json_object_get(rootJ, "Ring Morph"));
        randomRingMorph = json_boolean_value(json_object_get(rootJ, "Randomize Ring Morph"));
        exitConfigOnConnect = json_boolean_value(json_object_get(rootJ, "Auto Exit"));
        ccwSceneSelection = json_boolean_value(json_object_get(rootJ, "CCW Scene Selection"));
        resetOnRun = json_boolean_value(json_object_get(rootJ, "Reset on Run"));
        clickFilterEnabled = json_boolean_value(json_object_get(rootJ, "Click Filter Enabled"));
        glowingInk = json_boolean_value(json_object_get(rootJ, "Glowing Ink"));
        vuLights = json_boolean_value(json_object_get(rootJ, "VU Lights"));

        //Set allowMultipleModes and forgetVoltage before loading modes
        optionInput.allowMultipleModes = json_boolean_value(json_object_get(rootJ, "Allow Multiple Modes"));
        optionInput.forgetVoltage = json_boolean_value(json_object_get(rootJ, "Forget Option Voltage"));

        json_t* opInputModesJ = json_object_get(rootJ, "Option Input Modes");
        json_t* inputModeJ; size_t inputModeIndex;
        json_array_foreach(opInputModesJ, inputModeIndex, inputModeJ) {
            if (json_boolean_value(json_object_get(inputModeJ, "Destination")))
                optionInput.setMode(static_cast<OptionInput::Modes>(inputModeIndex));
        }

        json_t* opDestinationsJ = json_object_get(rootJ, "Operator Destinations");
        json_t* destinationJ; size_t destinationIndex;
        int i = 0, j = 0, k = 0;
        json_array_foreach(opDestinationsJ, destinationIndex, destinationJ) {
            opDestinations[i][j][k] = json_boolean_value(json_object_get(destinationJ, "Destination"));
            k++;
            if (k > 2) {
                k = 0;
                j++;
                if (j > 3) {
                    j = 0;
                    i++;
                }
            }
        }

        json_t* algoNamesJ = json_object_get(rootJ, "Algorithm Names");
        json_t* nameJ; size_t sixteenToTwelve;
        json_array_foreach(algoNamesJ, sixteenToTwelve, nameJ) {
            algoName[sixteenToTwelve] = json_integer_value(json_object_get(nameJ, "Name"));
        }

        json_t* forcedCarriersJ = json_object_get(rootJ, "Forced Carriers");
        json_t* forcedCarrierJ;
        size_t forcedCarrierIndex;
        i = j = 0;
        json_array_foreach(forcedCarriersJ, forcedCarrierIndex, forcedCarrierJ) {
            forcedCarrier[i][j] = json_boolean_value(json_object_get(forcedCarrierJ, "Forced Carrier"));
            j++;
            if (j > 3) {
                j = 0;
                i++;
            }
        }

        json_t* horizontalConnectionsJ = json_object_get(rootJ, "Operators Enabled");
        json_t* connectionJ;
        size_t horizontalConnectionIndex;
        i = j = 0;
        json_array_foreach(horizontalConnectionsJ, horizontalConnectionIndex, connectionJ) {
            horizontalDestinations[i][j] = !json_boolean_value(json_object_get(connectionJ, "Enabled Op"));
            j++;
            if (j > 3) {
                j = 0;
                i++;
            }
        }

        //Legacy opDisabled
        json_t* opDisabledJ = json_object_get(rootJ, "Operators Disabled");
        json_t* disabledOpJ;
        size_t disabledOpIndex;
        i = j = 0;
        json_array_foreach(opDisabledJ, disabledOpIndex, disabledOpJ) {
            horizontalDestinations[i][j] = json_boolean_value(json_object_get(disabledOpJ, "Disabled Op"));
            j++;
            if (j > 3) {
                j = 0;
                i++;
            }
        }

        graphDirty = true;
    }
};

template < typename MODULE >
struct AlgoScreenWidget : FramebufferWidget {
    struct AlgoDrawWidget : LightWidget {
        MODULE* module;
        alGraph graphs[3];
        bool firstRun = true;
        std::shared_ptr<Font> font;
        float textBounds[4];

        float xOrigin = box.size.x / 2.f;
        float yOrigin = box.size.y / 2.f;
   
        const NVGcolor NODE_FILL_COLOR = nvgRGBA(0x40, 0x36, 0x4a, 0xff);
        const NVGcolor NODE_STROKE_COLOR = nvgRGB(26, 26, 26);
        const NVGcolor TEXT_COLOR = nvgRGBA(0xb2, 0xa9, 0xb9, 0xff);
        const NVGcolor EDGE_COLOR = nvgRGBA(0x9a,0x9a,0x6f,0xff);

        NVGcolor nodeFillColor = NODE_FILL_COLOR;
        NVGcolor nodeStrokeColor = NODE_STROKE_COLOR;
        NVGcolor textColor = TEXT_COLOR;
        NVGcolor edgeColor = EDGE_COLOR;
        float borderStroke = 0.45f;
        float labelStroke = 0.5f;
        float nodeStroke = 0.75f;
        float edgeStroke = 0.925f;
        static constexpr float arrowStroke1 = (2.65f/4.f) + (1.f/3.f);
        static constexpr float arrowStroke2 = (7.65f/4.f) + (1.f/3.f);

        AlgoDrawWidget(MODULE* module) {
            this->module = module;
            font = APP->window->loadFont(asset::plugin(pluginInstance, "res/terminal-grotesque.ttf"));
        }

        void drawEdges(NVGcontext* ctx, alGraph source, alGraph destination, float morph) {
            if (source >= destination)
                renderEdges(ctx, source, destination, morph, false);
            else 
                renderEdges(ctx, destination, source, morph, true);
        }

        void renderEdges(NVGcontext* ctx, alGraph mostEdges, alGraph leastEdges, float morph, bool flipped) {
            for (int i = 0; i < mostEdges.numEdges; i++) {
                Edge edge[2];
                Arrow arrow[2];
                nvgBeginPath(ctx);
                if (leastEdges.numEdges == 0) {
                    if (!flipped) {
                        edge[0] = mostEdges.edges[i];
                        edge[1] = leastEdges.edges[i];
                        nvgMoveTo(ctx, crossfade(edge[0].moveCoords.x, xOrigin, morph), crossfade(edge[0].moveCoords.y, yOrigin, morph));
                        edgeColor.a = crossfade(EDGE_COLOR.a, 0x00, morph);
                    }
                    else {
                        edge[0] = leastEdges.edges[i];
                        edge[1] = mostEdges.edges[i];
                        nvgMoveTo(ctx, crossfade(xOrigin, edge[1].moveCoords.x, morph), crossfade(yOrigin, edge[1].moveCoords.y, morph));
                        edgeColor.a = crossfade(0x00, EDGE_COLOR.a, morph);
                    }
                    arrow[0] = mostEdges.arrows[i];
                    arrow[1] = leastEdges.arrows[i];
                }
                else if (i < leastEdges.numEdges) {
                    if (!flipped) {
                        edge[0] = mostEdges.edges[i];
                        edge[1] = leastEdges.edges[i];
                    }
                    else {
                        edge[0] = leastEdges.edges[i];
                        edge[1] = mostEdges.edges[i];
                    }
                    nvgMoveTo(ctx, crossfade(edge[0].moveCoords.x, edge[1].moveCoords.x, morph), crossfade(edge[0].moveCoords.y, edge[1].moveCoords.y, morph));
                    edgeColor = nvgRGBA(0x9a, 0x9a, 0x6f, 0xff);
                    arrow[0] = mostEdges.arrows[i];
                    arrow[1] = leastEdges.arrows[i];
                }
                else {
                    if (!flipped) {
                        edge[0] = mostEdges.edges[i];
                        edge[1] = leastEdges.edges[std::max(0, leastEdges.numEdges - 1)];
                    }
                    else {
                        edge[0] = leastEdges.edges[std::max(0, leastEdges.numEdges - 1)];
                        edge[1] = mostEdges.edges[i];
                    }
                    nvgMoveTo(ctx, crossfade(edge[0].moveCoords.x, edge[1].moveCoords.x, morph), crossfade(edge[0].moveCoords.y, edge[1].moveCoords.y, morph));
                    edgeColor = EDGE_COLOR;
                    arrow[0] = mostEdges.arrows[i];
                    arrow[1] = leastEdges.arrows[std::max(0, leastEdges.numEdges - 1)];
                }
                if (edge[0] >= edge[1]) {
                    reticulateEdge(ctx, edge[0], edge[1], morph, false);
                }
                else {
                    reticulateEdge(ctx, edge[1], edge[0], morph, true);
                }

                nvgStrokeColor(ctx, edgeColor);
                nvgStrokeWidth(ctx, edgeStroke);
                nvgStroke(ctx);

                nvgBeginPath(ctx);
                reticulateArrow(ctx, arrow[0], arrow[1], morph, flipped);
                nvgFillColor(ctx, edgeColor);
                nvgFill(ctx);
                nvgStrokeColor(ctx, edgeColor);
                nvgStrokeWidth(ctx, arrowStroke1);
                nvgStroke(ctx);
            }
        }

        void reticulateEdge(NVGcontext* ctx, Edge mostCurved, Edge leastCurved, float morph, bool flipped) {
            for (int j = 0; j < mostCurved.curveLength; j++) {
                if (leastCurved.curveLength == 0) {
                    if (!flipped)
                        nvgBezierTo(ctx, crossfade(mostCurved.curve[j][0].x, xOrigin, morph), crossfade(mostCurved.curve[j][0].y, yOrigin, morph), crossfade(mostCurved.curve[j][1].x, xOrigin, morph), crossfade(mostCurved.curve[j][1].y, yOrigin, morph), crossfade(mostCurved.curve[j][2].x, xOrigin, morph), crossfade(mostCurved.curve[j][2].y, yOrigin, morph));
                    else
                        nvgBezierTo(ctx, crossfade(xOrigin, mostCurved.curve[j][0].x, morph), crossfade(yOrigin, mostCurved.curve[j][0].y, morph), crossfade(xOrigin, mostCurved.curve[j][1].x, morph), crossfade(yOrigin, mostCurved.curve[j][1].y, morph), crossfade(xOrigin, mostCurved.curve[j][2].x, morph), crossfade(yOrigin, mostCurved.curve[j][2].y, morph));
                }
                else if (j < leastCurved.curveLength) {
                    if (!flipped)
                        nvgBezierTo(ctx, crossfade(mostCurved.curve[j][0].x, leastCurved.curve[j][0].x, morph), crossfade(mostCurved.curve[j][0].y, leastCurved.curve[j][0].y, morph), crossfade(mostCurved.curve[j][1].x, leastCurved.curve[j][1].x, morph), crossfade(mostCurved.curve[j][1].y, leastCurved.curve[j][1].y, morph), crossfade(mostCurved.curve[j][2].x, leastCurved.curve[j][2].x, morph), crossfade(mostCurved.curve[j][2].y, leastCurved.curve[j][2].y, morph));
                    else
                        nvgBezierTo(ctx, crossfade(leastCurved.curve[j][0].x, mostCurved.curve[j][0].x, morph), crossfade(leastCurved.curve[j][0].y, mostCurved.curve[j][0].y, morph), crossfade(leastCurved.curve[j][1].x, mostCurved.curve[j][1].x, morph), crossfade(leastCurved.curve[j][1].y, mostCurved.curve[j][1].y, morph), crossfade(leastCurved.curve[j][2].x, mostCurved.curve[j][2].x, morph), crossfade(leastCurved.curve[j][2].y, mostCurved.curve[j][2].y, morph));
                }
                else {
                    if (!flipped)
                        nvgBezierTo(ctx, crossfade(mostCurved.curve[j][0].x, leastCurved.curve[leastCurved.curveLength - 1][0].x, morph), crossfade(mostCurved.curve[j][0].y, leastCurved.curve[leastCurved.curveLength - 1][0].y, morph), crossfade(mostCurved.curve[j][1].x, leastCurved.curve[leastCurved.curveLength - 1][1].x, morph), crossfade(mostCurved.curve[j][1].y, leastCurved.curve[leastCurved.curveLength - 1][1].y, morph), crossfade(mostCurved.curve[j][2].x, leastCurved.curve[leastCurved.curveLength - 1][2].x, morph), crossfade(mostCurved.curve[j][2].y, leastCurved.curve[leastCurved.curveLength - 1][2].y, morph));
                    else
                        nvgBezierTo(ctx, crossfade(leastCurved.curve[leastCurved.curveLength - 1][0].x, mostCurved.curve[j][0].x, morph), crossfade(leastCurved.curve[leastCurved.curveLength - 1][0].y, mostCurved.curve[j][0].y, morph), crossfade(leastCurved.curve[leastCurved.curveLength - 1][1].x, mostCurved.curve[j][1].x, morph), crossfade(leastCurved.curve[leastCurved.curveLength - 1][1].y, mostCurved.curve[j][1].y, morph), crossfade(leastCurved.curve[leastCurved.curveLength - 1][2].x, mostCurved.curve[j][2].x, morph), crossfade(leastCurved.curve[leastCurved.curveLength - 1][2].y, mostCurved.curve[j][2].y, morph));
                }
            }
        }

        void reticulateArrow(NVGcontext* ctx, Arrow mostGregarious, Arrow leastGregarious, float morph, bool flipped) {
            if (leastGregarious.moveCoords.x == 0) {
                if (!flipped)
                    nvgMoveTo(ctx, crossfade(mostGregarious.moveCoords.x, xOrigin, morph), crossfade(mostGregarious.moveCoords.y, yOrigin, morph));
                else
                    nvgMoveTo(ctx, crossfade(xOrigin, mostGregarious.moveCoords.x, morph), crossfade(yOrigin, mostGregarious.moveCoords.y, morph));
                for (int j = 0; j < 9; j++) {
                    if (!flipped)
                        nvgLineTo(ctx, crossfade(mostGregarious.lines[j].x, xOrigin, morph), crossfade(mostGregarious.lines[j].y, yOrigin, morph));
                    else
                        nvgLineTo(ctx, crossfade(xOrigin, mostGregarious.lines[j].x, morph), crossfade(yOrigin, mostGregarious.lines[j].y, morph));
                }
            }
            else {
                if (!flipped)
                    nvgMoveTo(ctx, crossfade(mostGregarious.moveCoords.x, leastGregarious.moveCoords.x, morph), crossfade(mostGregarious.moveCoords.y, leastGregarious.moveCoords.y, morph));
                else
                    nvgMoveTo(ctx, crossfade(leastGregarious.moveCoords.x, mostGregarious.moveCoords.x, morph), crossfade(leastGregarious.moveCoords.y, mostGregarious.moveCoords.y, morph));
                for (int j = 0; j < 9; j++) {
                    if (!flipped)
                        nvgLineTo(ctx, crossfade(mostGregarious.lines[j].x, leastGregarious.lines[j].x, morph), crossfade(mostGregarious.lines[j].y, leastGregarious.lines[j].y, morph));
                    else
                        nvgLineTo(ctx, crossfade(leastGregarious.lines[j].x, mostGregarious.lines[j].x, morph), crossfade(leastGregarious.lines[j].y, mostGregarious.lines[j].y, morph));
                }
            }
        }

        void draw(const Widget::DrawArgs& args) override {
            if (!module) return;

            xOrigin = box.size.x / 2.f;
            yOrigin = box.size.y / 2.f;

            for (int i = 0; i < 3; i++) {
                int name = module->sixteenToTwelve[module->algoName[i].to_ullong()];
                if (name != -1)
                    graphs[i] = alGraph(module->sixteenToTwelve[(int)module->algoName[i].to_ullong()]);
                else
                    graphs[i] = alGraph(0);
            }
              
            int scene = module->configMode ? module->configScene : module->centerMorphScene[0];
            int morphScene = module->forwardMorphScene[0];
            float morph = module->relativeMorphMagnitude[0];

            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, box.getTopLeft().x, box.getTopLeft().y, box.size.x, box.size.y, 3.675f);
            nvgStrokeWidth(args.vg, borderStroke);
            nvgStroke(args.vg);

            // Draw nodes
            float radius = 8.35425f;
            nvgBeginPath(args.vg);
            for (int i = 0; i < 4; i++) {
                if (module->configMode)    //Display state without morph
                    nvgCircle(args.vg, graphs[scene].nodes[i].coords.x, graphs[scene].nodes[i].coords.y, radius);
                else {  //Display morphed state
                    nvgCircle(args.vg,  crossfade(graphs[scene].nodes[i].coords.x, graphs[morphScene].nodes[i].coords.x, morph),
                                        crossfade(graphs[scene].nodes[i].coords.y, graphs[morphScene].nodes[i].coords.y, morph),
                                        radius);
                }
            }
            nvgFillColor(args.vg, nodeFillColor);
            nvgFill(args.vg);
            nvgStrokeColor(args.vg, nodeStrokeColor);
            nvgStrokeWidth(args.vg, nodeStroke);
            nvgStroke(args.vg);

            // Draw node numbers
            nvgBeginPath(args.vg);
            nvgFontSize(args.vg, 10.f);
            nvgFontFaceId(args.vg, font->handle);
            nvgFillColor(args.vg, textColor);
            for (int i = 0; i < 4; i++) {
                std::string s = std::to_string(i + 1);
                char const *id = s.c_str();
                nvgTextBounds(args.vg, graphs[scene].nodes[i].coords.x, graphs[scene].nodes[i].coords.y, id, id + 1, textBounds);
                float xOffset = (textBounds[2] - textBounds[0]) / 2.f;
                float yOffset = (textBounds[3] - textBounds[1]) / 3.25f;
                if (module->configMode)    //Display state without morph
                    nvgText(args.vg, graphs[scene].nodes[i].coords.x - xOffset, graphs[scene].nodes[i].coords.y + yOffset, id, id + 1);
                else {  //Display moprhed state
                    nvgText(args.vg,  crossfade(graphs[scene].nodes[i].coords.x, graphs[morphScene].nodes[i].coords.x, morph) - xOffset,
                                        crossfade(graphs[scene].nodes[i].coords.y, graphs[morphScene].nodes[i].coords.y, morph) + yOffset,
                                        id, id + 1);
                }
            }

            // Draw edges +/ arrows
            if (module->configMode) {
                // Draw edges
                nvgBeginPath(args.vg);
                for (int i = 0; i < graphs[scene].numEdges; i++) {
                    Edge edge = graphs[scene].edges[i];
                    nvgMoveTo(args.vg, edge.moveCoords.x, edge.moveCoords.y);
                    for (int j = 0; j < edge.curveLength; j++) {
                        nvgBezierTo(args.vg, edge.curve[j][0].x, edge.curve[j][0].y, edge.curve[j][1].x, edge.curve[j][1].y, edge.curve[j][2].x, edge.curve[j][2].y);
                    }
                }
                edgeColor = EDGE_COLOR;
                nvgStrokeColor(args.vg, edgeColor);
                nvgStrokeWidth(args.vg, edgeStroke);
                nvgStroke(args.vg);
                // Draw arrows
                for (int i = 0; i < graphs[scene].numEdges; i++) {
                    nvgBeginPath(args.vg);
                    nvgMoveTo(args.vg, graphs[scene].arrows[i].moveCoords.x, graphs[scene].arrows[i].moveCoords.y);
                    for (int j = 0; j < 9; j++)
                        nvgLineTo(args.vg, graphs[scene].arrows[i].lines[j].x, graphs[scene].arrows[i].lines[j].y);
                    edgeColor = EDGE_COLOR;
                    nvgFillColor(args.vg, edgeColor);
                    nvgFill(args.vg);
                    nvgStrokeColor(args.vg, edgeColor);
                    nvgStrokeWidth(args.vg, arrowStroke1);
                    nvgStroke(args.vg);
                }
            }
            else {
                // Draw edges AND arrows
                drawEdges(args.vg, graphs[scene], graphs[morphScene], morph);
            }                
        }
    };

    MODULE* module;
    AlgoDrawWidget* w;

    AlgoScreenWidget(MODULE* module) {
        this->module = module;
        w = new AlgoDrawWidget(module);
        addChild(w);
    }

    void step() override {
        if (module && module->graphDirty) {
            FramebufferWidget::dirty = true;
            w->box.size = box.size;
            module->graphDirty = false;
        }
        FramebufferWidget::step();
    }
};

struct OptionModeItem : MenuItem {
    Algomorph4 *module;
    Algomorph4::OptionInput::Modes mode;

    void onAction(const event::Action &e) override {
        if (module->optionInput.mode[mode])
            module->optionInput.unsetMode(mode);
        else
            module->optionInput.setMode(mode);
    }
};

//Order must match OptionInput::Modes
std::string OptionModeLabels[Algomorph4::OptionInput::NUM_MODES] = {    "Morph CV",
                                                                        "Morph CV Attenuverter",
                                                                        "Run",
                                                                        "Clock",
                                                                        "Reverse Clock",
                                                                        "Reset",
                                                                        "Modulation Gain Attenuverter",
                                                                        "Operator Gain Attenuverter",
                                                                        "Sum Gain Attenuverter",
                                                                        "Algorithm Offset",
                                                                        "Wildcard Modulator",
                                                                        "Carrier",
                                                                        "Shadow -> 1",
                                                                        "Shadow -> 2",
                                                                        "Shadow -> 3",
                                                                        "Shadow -> 4",
                                                                        "Click Filter Strength",
                                                                        "Hyper Morph CV",
                                                                        "Screen Brightness",
                                                                        "Connection Brightness"};

template < typename MODULE >
void createWildcardInputMenu(MODULE* module, ui::Menu* menu) {
    for (int i = Algomorph4::OptionInput::WILDCARD_MOD; i <= Algomorph4::OptionInput::WILDCARD_SUM; i++)
        menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[i], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[i]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(i)));
}

template < typename MODULE >
struct WildcardInputMenuItem : MenuItem {
	MODULE* module;
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		createWildcardInputMenu(module, menu);
		return menu;
	}
};

template < typename MODULE >
void createShadowInputMenu(MODULE* module, ui::Menu* menu) {
    for (int i = Algomorph4::OptionInput::SHADOW; i <= Algomorph4::OptionInput::SHADOW + 3; i++)
        menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[i], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[i]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(i)));
}

template < typename MODULE >
struct ShadowInputMenuItem : MenuItem {
	MODULE* module;
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		createShadowInputMenu(module, menu);
		return menu;
	}
};

template < typename MODULE >
void createBrightnessInputMenu(MODULE* module, ui::Menu* menu) {
    for (int i = Algomorph4::OptionInput::SCREEN_BRIGHTNESS; i <= Algomorph4::OptionInput::CONNECTION_BRIGHTNESS; i++)
        menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[i], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[i]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(i)));
}

template < typename MODULE >
struct BrightnessInputMenuItem : MenuItem {
	MODULE* module;
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		createBrightnessInputMenu(module, menu);
		return menu;
	}
};

struct ResetOnRunItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->resetOnRun ^= true;
    }
};

struct RunSilencerItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->runSilencer ^= true;
    }
};

template < typename MODULE >
void createRunOptionMenu(MODULE* module, ui::Menu* menu) {
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[2], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[2]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(2)));
    menu->addChild(new MenuSeparator());
    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Run Options"));

    ResetOnRunItem *resetOnRunItem = createMenuItem<ResetOnRunItem>("Reset on Run trigger", CHECKMARK(module->resetOnRun));
    resetOnRunItem->module = module;
    menu->addChild(resetOnRunItem);
    
    RunSilencerItem *runSilencerItem = createMenuItem<RunSilencerItem>("Silence when not running", CHECKMARK(module->runSilencer));
    runSilencerItem->module = module;
    menu->addChild(runSilencerItem);
}

template < typename MODULE >
struct RunOptionMenuItem : MenuItem {
	MODULE* module;
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		createRunOptionMenu(module, menu);
		return menu;
	}
};

template < typename MODULE >
void createOptionInputMenu(MODULE* module, ui::Menu* menu) {
    // for (int i = 0; i < Algomorph4::OptionInput::Modes::NUM_MODES; i++)
    //     menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[i], &OptionModeItem::module, module, &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(i)));
    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "5th Operator"));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[11], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[11]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(11)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[10], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[10]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(10)));
    menu->addChild(construct<ShadowInputMenuItem<Algomorph4>>(&MenuItem::text, "Shadow modes", &MenuItem::rightText, RIGHT_ARROW, &ShadowInputMenuItem<Algomorph4>::module, module));

    menu->addChild(new MenuSeparator());
    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Trigger"));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[3], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[3]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(3)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[4], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[4]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(4)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[5], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[5]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(5)));
    menu->addChild(construct<RunOptionMenuItem<Algomorph4>>(&MenuItem::text, "Run", &MenuItem::rightText, RIGHT_ARROW, &RunOptionMenuItem<Algomorph4>::module, module));
   
    menu->addChild(new MenuSeparator());
    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "CV"));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[0], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[0]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(0)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[1], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[1]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(1)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[6], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[6]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(6)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[7], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[7]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(7)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[8], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[8]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(8)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[9], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[9]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(9)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[16], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[16]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(16)));
    menu->addChild(construct<OptionModeItem>(&MenuItem::text, OptionModeLabels[17], &OptionModeItem::module, module, &OptionModeItem::rightText, CHECKMARK(module->optionInput.mode[17]), &OptionModeItem::mode, static_cast<Algomorph4::OptionInput::Modes>(17)));
    menu->addChild(construct<BrightnessInputMenuItem<Algomorph4>>(&MenuItem::text, "Light modes", &MenuItem::rightText, RIGHT_ARROW, &BrightnessInputMenuItem<Algomorph4>::module, module));
}

template < typename MODULE >
struct OptionInputMenuItem : MenuItem {
	MODULE* module;
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		createOptionInputMenu(module, menu);
		return menu;
	}
};

struct AllowMultipleModesItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->optionInput.toggleAllowMultipleModes();
    }
};

struct ForgetOptionVoltageItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->optionInput.toggleForgetVoltage();
    }
};


struct ResetSceneItem : MenuItem {
    Algomorph4 *module;
    int scene;

    void onAction(const event::Action &e) override {
        module->resetScene = scene;
    }
};

template < typename MODULE >
void createResetSceneMenu(MODULE* module, ui::Menu* menu) {
    menu->addChild(construct<ResetSceneItem>(&MenuItem::text, "1", &ResetSceneItem::module, module, &ResetSceneItem::rightText, CHECKMARK(module->resetScene == 0), &ResetSceneItem::scene, 0));
    menu->addChild(construct<ResetSceneItem>(&MenuItem::text, "2", &ResetSceneItem::module, module, &ResetSceneItem::rightText, CHECKMARK(module->resetScene == 1), &ResetSceneItem::scene, 1));
    menu->addChild(construct<ResetSceneItem>(&MenuItem::text, "3", &ResetSceneItem::module, module, &ResetSceneItem::rightText, CHECKMARK(module->resetScene == 2), &ResetSceneItem::scene, 2));
}

template < typename MODULE >
struct ResetSceneMenuItem : MenuItem {
	MODULE* module;
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		createResetSceneMenu(module, menu);
		return menu;
	}
};

struct AllowHorizontalItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->horizontalAllowed = false;
    }
};

struct DisallowHorizontalItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->horizontalAllowed = true;
    }
};

struct RandomizeAllowHorizontalItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->randomAllowHorizontal ^= true;
    }
};

struct RingMorphItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->ringMorph ^= true;
    }
};

struct RandomizeRingMorphItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->randomRingMorph ^= true;
    }
};

template < typename MODULE >
void createRandomizationMenu(MODULE* module, ui::Menu* menu) {    
    RandomizeRingMorphItem *ramdomizeRingMorphItem = createMenuItem<RandomizeRingMorphItem>("Enabling Ring Morph", CHECKMARK(module->randomRingMorph));
    ramdomizeRingMorphItem->module = module;
    menu->addChild(ramdomizeRingMorphItem);

    RandomizeAllowHorizontalItem *ramdomizeAllowHorizontalItem = createMenuItem<RandomizeAllowHorizontalItem>("Setting A/B Mode", CHECKMARK(module->randomAllowHorizontal));
    ramdomizeAllowHorizontalItem->module = module;
    menu->addChild(ramdomizeAllowHorizontalItem);
}

template < typename MODULE >
struct RandomizationMenuItem : MenuItem {
	MODULE* module;
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		createRandomizationMenu(module, menu);
		return menu;
	}
};

struct ExitConfigItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->exitConfigOnConnect ^= true;
    }
};

struct CCWScenesItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->ccwSceneSelection ^= true;
    }
};

struct ClickFilterEnabledItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->clickFilterEnabled ^= true;
    }
};

struct GlowingInkItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->glowingInk ^= true;
    }
};

struct VULightsItem : MenuItem {
    Algomorph4 *module;
    void onAction(const event::Action &e) override {
        module->vuLights ^= true;
    }
};

// struct DebugItem : MenuItem {
//     Algomorph4 *module;
//     void onAction(const event::Action &e) override {
//         module->debug ^= true;
//     }
// };
template < typename MODULE >
struct ClickFilterSlider : ui::Slider {
	struct ClickFilterQuantity : Quantity {
		MODULE* module;
		float v = -1.f;

		ClickFilterQuantity(MODULE* module) {
			this->module = module;
		}
		void setValue(float value) override {
			v = clamp(value, 16.f, 7500.f);
			module->clickFilterSlew = v;
		}
		float getValue() override {
			v = module->clickFilterSlew;
			return v;
		}
		float getDefaultValue() override {
			return DEF_CLICK_FILTER_SLEW;
		}
		float getMinValue() override {
			return 16.f;
		}
		float getMaxValue() override {
			return 7500.f;
		}
		float getDisplayValue() override {
			return getValue();
		}
		std::string getDisplayValueString() override {
			int i = int(getValue());
			return string::f("%i", i);
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue);
		}
		std::string getLabel() override {
			return "Click Filter Slew";
		}
		std::string getUnit() override {
			return "Hz";
		}
	};

	ClickFilterSlider(MODULE* module) {
		quantity = new ClickFilterQuantity(module);
	}
	~ClickFilterSlider() {
		delete quantity;
	}
	void onDragMove(const event::DragMove& e) override {
		if (quantity) {
			quantity->moveScaledValue(0.002f * e.mouseDelta.x);
		}
	}
};

template < typename MODULE >
void createClickFilterMenu(MODULE* module, ui::Menu* menu) {
    ClickFilterEnabledItem *clickFilterEnabledItem = createMenuItem<ClickFilterEnabledItem>("Disable Click Filter", CHECKMARK(!module->clickFilterEnabled));
    clickFilterEnabledItem->module = module;
    menu->addChild(clickFilterEnabledItem);
    ClickFilterSlider<MODULE>* clickFilterSlider = new ClickFilterSlider<MODULE>(module);
    clickFilterSlider->box.size.x = 200.0;
    menu->addChild(clickFilterSlider);
}

template < typename MODULE >
struct ClickFilterMenuItem : MenuItem {
	MODULE* module;
	
	Menu* createChildMenu() override {
		Menu* menu = new Menu;
		createClickFilterMenu(module, menu);
		return menu;
	}
};

struct Algomorph4Widget : ModuleWidget {
    std::vector<Vec> SceneButtonCenters = {  {mm2px(53.831), mm2px(46.862)},
                                            {mm2px(53.831), mm2px(55.262)},
                                            {mm2px(53.831), mm2px(63.662)} };
    std::vector<Vec> OpButtonCenters = { {mm2px(22.416), mm2px(49.614)},
                                        {mm2px(22.416), mm2px(60.634)},
                                        {mm2px(22.416), mm2px(71.655)},
                                        {mm2px(22.416), mm2px(82.677)} };
    std::vector<Vec> ModButtonCenters = {{mm2px(38.732), mm2px(49.614)},
                                        {mm2px(38.732), mm2px(60.634)},
                                        {mm2px(38.732), mm2px(71.655)},
                                        {mm2px(38.732), mm2px(82.677)} };
    DLXGlowingInk* ink;
    
    DLXRingIndicator* createRingIndicator(Vec pos, float r, engine::Module* module, int firstLightId, float s = 0) {
        DLXRingIndicator* o = new DLXRingIndicator(r, s);
        o->box.pos = pos;
        o->module = module;
        o->firstLightId = firstLightId;
        return o;
    }

    DLXRingIndicator* createRingIndicatorCentered(Vec pos, float r, engine::Module* module, int firstLightId, float s = 0) {
        DLXRingIndicator* o = createRingIndicator(pos, r, module, firstLightId, s);
        o->box.pos.x -= r;
        o->box.pos.y -= r;
        return o;
    }

    Algomorph4Widget(Algomorph4* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Algomorph.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH * 2, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 365)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - RACK_GRID_WIDTH * 2, 365)));

        ink = createSvgLight<DLXGlowingInk>(Vec(0,0), module, Algomorph4::GLOWING_INK);
        ink->visible = false;
        addChildBottom(ink);

        AlgoScreenWidget<Algomorph4>* screenWidget = new AlgoScreenWidget<Algomorph4>(module);
        screenWidget->box.pos = mm2px(Vec(11.333, 7.195));
        screenWidget->box.size = mm2px(Vec(38.295, 31.590));
        addChild(screenWidget);
        //Place backlight _above_ in scene in order for brightness to affect screenWidget
        addChild(createBacklight<DLXScreenMultiLight>(mm2px(Vec(11.333, 7.195)), mm2px(Vec(38.295, 31.590)), module, Algomorph4::DISPLAY_BACKLIGHT));

        addChild(createRingLightCentered<DLXMultiLight>(mm2px(Vec(30.760, 45.048)), 8.862, module, Algomorph4::SCREEN_BUTTON_RING_LIGHT, .4));
        addChild(createParamCentered<DLXPurpleButton>(mm2px(Vec(30.760, 45.048)), module, Algomorph4::SCREEN_BUTTON));
        addChild(createSvgSwitchLightCentered<DLXScreenButtonLight>(mm2px(Vec(30.760, 45.048)), module, Algomorph4::SCREEN_BUTTON_LIGHT, Algomorph4::SCREEN_BUTTON));

        addChild(createRingLightCentered<DLXMultiLight>(SceneButtonCenters[0], 8.862, module, Algomorph4::SCENE_LIGHTS + 0, .75));
        addChild(createParamCentered<DLXTL1105B>(SceneButtonCenters[0], module, Algomorph4::SCENE_BUTTONS + 0));
        addChild(createSvgSwitchLightCentered<DLX1Light>(SceneButtonCenters[0], module, Algomorph4::ONE_LIGHT, Algomorph4::SCENE_BUTTONS + 0));

        addChild(createRingLightCentered<DLXMultiLight>(SceneButtonCenters[1], 8.862, module, Algomorph4::SCENE_LIGHTS + 3, .75));
        addChild(createParamCentered<DLXTL1105B>(SceneButtonCenters[1], module, Algomorph4::SCENE_BUTTONS + 1));
        addChild(createSvgSwitchLightCentered<DLX2Light>(SceneButtonCenters[1], module, Algomorph4::TWO_LIGHT, Algomorph4::SCENE_BUTTONS + 1));

        addChild(createRingLightCentered<DLXMultiLight>(SceneButtonCenters[2], 8.862, module, Algomorph4::SCENE_LIGHTS + 6, .75));
        addChild(createParamCentered<DLXTL1105B>(SceneButtonCenters[2], module, Algomorph4::SCENE_BUTTONS + 2));
        addChild(createSvgSwitchLightCentered<DLX3Light>(SceneButtonCenters[2], module, Algomorph4::THREE_LIGHT, Algomorph4::SCENE_BUTTONS + 2));

        addInput(createInput<DLXPortPoly>(mm2px(Vec(3.778, 42.771)), module, Algomorph4::OPTION_INPUT));
        addInput(createInput<DLXPortPoly>(mm2px(Vec(3.778, 52.792)), module, Algomorph4::MORPH_INPUT));
        addInput(createInput<DLXPortPoly>(mm2px(Vec(3.778, 62.812)), module, Algomorph4::SCENE_ADV_INPUT));
        addInput(createInput<DLXPortPoly>(mm2px(Vec(3.778, 72.834)), module, Algomorph4::RESET_INPUT));

        DLXKnobLight* kl = createLight<DLXKnobLight>(mm2px(Vec(20.305, 96.721)), module, Algomorph4::KNOB_LIGHT);
        addChild(createLightKnob(mm2px(Vec(20.305, 96.721)), module, Algomorph4::MORPH_KNOB, kl));
        addChild(kl);

        addOutput(createOutput<DLXPortPolyOut>(mm2px(Vec(50.182, 73.040)), module, Algomorph4::SUM_OUTPUT));

        addChild(createRingLightCentered<DLXYellowLight>(mm2px(Vec(30.545, 87.440)), 8.862, module, Algomorph4::EDIT_LIGHT, .4));
        addChild(createParamCentered<DLXPurpleButton>(mm2px(Vec(30.545, 87.440)), module, Algomorph4::EDIT_BUTTON));
        addChild(createSvgSwitchLightCentered<DLXPencilLight>(mm2px(Vec(30.545 - 0.01, 87.440 - 0.387)), module, Algomorph4::EDIT_LIGHT, Algomorph4::EDIT_BUTTON));

        addInput(createInput<DLXPortPoly>(mm2px(Vec(3.778, 85.526)), module, Algomorph4::OPERATOR_INPUTS + 0));
        addInput(createInput<DLXPortPoly>(mm2px(Vec(3.778, 95.546)), module, Algomorph4::OPERATOR_INPUTS + 1));
        addInput(createInput<DLXPortPoly>(mm2px(Vec(3.778, 105.567)), module, Algomorph4::OPERATOR_INPUTS + 2));
        addInput(createInput<DLXPortPoly>(mm2px(Vec(3.778, 115.589)), module, Algomorph4::OPERATOR_INPUTS + 3));

        addOutput(createOutput<DLXPortPolyOut>(mm2px(Vec(50.184, 85.526)), module, Algomorph4::MODULATOR_OUTPUTS + 0));
        addOutput(createOutput<DLXPortPolyOut>(mm2px(Vec(50.184, 95.546)), module, Algomorph4::MODULATOR_OUTPUTS + 1));
        addOutput(createOutput<DLXPortPolyOut>(mm2px(Vec(50.184, 105.567)), module, Algomorph4::MODULATOR_OUTPUTS + 2));
        addOutput(createOutput<DLXPortPolyOut>(mm2px(Vec(50.184, 115.589)), module, Algomorph4::MODULATOR_OUTPUTS + 3));

        ConnectionBgWidget* connectionBgWidget = new ConnectionBgWidget(OpButtonCenters, ModButtonCenters);
        connectionBgWidget->box.pos = this->box.pos;
        connectionBgWidget->box.size = this->box.size;
        addChild(connectionBgWidget);

        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[0], ModButtonCenters[0], module, Algomorph4::H_CONNECTION_LIGHTS + 0));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[1], ModButtonCenters[1], module, Algomorph4::H_CONNECTION_LIGHTS + 3));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[2], ModButtonCenters[2], module, Algomorph4::H_CONNECTION_LIGHTS + 6));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[3], ModButtonCenters[3], module, Algomorph4::H_CONNECTION_LIGHTS + 9));

        addChild(createLineLight<DLXRedLight>(OpButtonCenters[0], ModButtonCenters[1], module, Algomorph4::D_DISABLE_LIGHTS + 0));
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[0], ModButtonCenters[2], module, Algomorph4::D_DISABLE_LIGHTS + 1));
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[0], ModButtonCenters[3], module, Algomorph4::D_DISABLE_LIGHTS + 2));

        addChild(createLineLight<DLXRedLight>(OpButtonCenters[1], ModButtonCenters[0], module, Algomorph4::D_DISABLE_LIGHTS + 3));
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[1], ModButtonCenters[2], module, Algomorph4::D_DISABLE_LIGHTS + 4));
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[1], ModButtonCenters[3], module, Algomorph4::D_DISABLE_LIGHTS + 5));
    
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[2], ModButtonCenters[0], module, Algomorph4::D_DISABLE_LIGHTS + 6));
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[2], ModButtonCenters[1], module, Algomorph4::D_DISABLE_LIGHTS + 7));
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[2], ModButtonCenters[3], module, Algomorph4::D_DISABLE_LIGHTS + 8));

        addChild(createLineLight<DLXRedLight>(OpButtonCenters[3], ModButtonCenters[0], module, Algomorph4::D_DISABLE_LIGHTS + 9));
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[3], ModButtonCenters[1], module, Algomorph4::D_DISABLE_LIGHTS + 10));
        addChild(createLineLight<DLXRedLight>(OpButtonCenters[3], ModButtonCenters[2], module, Algomorph4::D_DISABLE_LIGHTS + 11));

        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[0], ModButtonCenters[1], module, Algomorph4::CONNECTION_LIGHTS + 0));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[0], ModButtonCenters[2], module, Algomorph4::CONNECTION_LIGHTS + 3));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[0], ModButtonCenters[3], module, Algomorph4::CONNECTION_LIGHTS + 6));

        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[1], ModButtonCenters[0], module, Algomorph4::CONNECTION_LIGHTS + 9));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[1], ModButtonCenters[2], module, Algomorph4::CONNECTION_LIGHTS + 12));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[1], ModButtonCenters[3], module, Algomorph4::CONNECTION_LIGHTS + 15));

        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[2], ModButtonCenters[0], module, Algomorph4::CONNECTION_LIGHTS + 18));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[2], ModButtonCenters[1], module, Algomorph4::CONNECTION_LIGHTS + 21));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[2], ModButtonCenters[3], module, Algomorph4::CONNECTION_LIGHTS + 24));

        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[3], ModButtonCenters[0], module, Algomorph4::CONNECTION_LIGHTS + 27));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[3], ModButtonCenters[1], module, Algomorph4::CONNECTION_LIGHTS + 30));
        addChild(createLineLight<DLXMultiLight>(OpButtonCenters[3], ModButtonCenters[2], module, Algomorph4::CONNECTION_LIGHTS + 33));
        
        addChild(createRingLightCentered<DLXMultiLight>(OpButtonCenters[0], 8.862, module, Algomorph4::OPERATOR_LIGHTS + 0));
        addChild(createRingLightCentered<DLXMultiLight>(OpButtonCenters[1], 8.862, module, Algomorph4::OPERATOR_LIGHTS + 3));
        addChild(createRingLightCentered<DLXMultiLight>(OpButtonCenters[2], 8.862, module, Algomorph4::OPERATOR_LIGHTS + 6));
        addChild(createRingLightCentered<DLXMultiLight>(OpButtonCenters[3], 8.862, module, Algomorph4::OPERATOR_LIGHTS + 9));

        addChild(createRingLightCentered<DLXMultiLight>(ModButtonCenters[0], 8.862, module, Algomorph4::MODULATOR_LIGHTS + 0));
        addChild(createRingLightCentered<DLXMultiLight>(ModButtonCenters[1], 8.862, module, Algomorph4::MODULATOR_LIGHTS + 3));
        addChild(createRingLightCentered<DLXMultiLight>(ModButtonCenters[2], 8.862, module, Algomorph4::MODULATOR_LIGHTS + 6));
        addChild(createRingLightCentered<DLXMultiLight>(ModButtonCenters[3], 8.862, module, Algomorph4::MODULATOR_LIGHTS + 9));

        addParam(createParamCentered<DLXPurpleButton>(OpButtonCenters[0], module, Algomorph4::OPERATOR_BUTTONS + 0));
        addParam(createParamCentered<DLXPurpleButton>(OpButtonCenters[1], module, Algomorph4::OPERATOR_BUTTONS + 1));
        addParam(createParamCentered<DLXPurpleButton>(OpButtonCenters[2], module, Algomorph4::OPERATOR_BUTTONS + 2));
        addParam(createParamCentered<DLXPurpleButton>(OpButtonCenters[3], module, Algomorph4::OPERATOR_BUTTONS + 3));

        addParam(createParamCentered<DLXPurpleButton>(ModButtonCenters[0], module, Algomorph4::MODULATOR_BUTTONS + 0));
        addParam(createParamCentered<DLXPurpleButton>(ModButtonCenters[1], module, Algomorph4::MODULATOR_BUTTONS + 1));
        addParam(createParamCentered<DLXPurpleButton>(ModButtonCenters[2], module, Algomorph4::MODULATOR_BUTTONS + 2));
        addParam(createParamCentered<DLXPurpleButton>(ModButtonCenters[3], module, Algomorph4::MODULATOR_BUTTONS + 3));
      
        addChild(createRingIndicatorCentered(OpButtonCenters[0], 8.862, module, Algomorph4::CARRIER_INDICATORS + 0));
        addChild(createRingIndicatorCentered(OpButtonCenters[1], 8.862, module, Algomorph4::CARRIER_INDICATORS + 3));
        addChild(createRingIndicatorCentered(OpButtonCenters[2], 8.862, module, Algomorph4::CARRIER_INDICATORS + 6));
        addChild(createRingIndicatorCentered(OpButtonCenters[3], 8.862, module, Algomorph4::CARRIER_INDICATORS + 9));
    }

    void appendContextMenu(Menu* menu) override {
        Algomorph4* module = dynamic_cast<Algomorph4*>(this->module);

        menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Auxiliary Input"));

		menu->addChild(construct<OptionInputMenuItem<Algomorph4>>(&MenuItem::text, "Input Function", &MenuItem::rightText, RIGHT_ARROW, &OptionInputMenuItem<Algomorph4>::module, module));
        
        AllowMultipleModesItem *allowMultipleModesItem = createMenuItem<AllowMultipleModesItem>("Allow multiple active functions", CHECKMARK(module->optionInput.allowMultipleModes));
        allowMultipleModesItem->module = module;
        menu->addChild(allowMultipleModesItem);
        
        ForgetOptionVoltageItem *forgetOptionVoltageItem = createMenuItem<ForgetOptionVoltageItem>("Remember voltage", CHECKMARK(!module->optionInput.forgetVoltage));
        forgetOptionVoltageItem->module = module;
        menu->addChild(forgetOptionVoltageItem);

        menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Audio"));
        
		menu->addChild(construct<ClickFilterMenuItem<Algomorph4>>(&MenuItem::text, "Click Filter", &MenuItem::rightText, RIGHT_ARROW, &ClickFilterMenuItem<Algomorph4>::module, module));


        RingMorphItem *ringMorphItem = createMenuItem<RingMorphItem>("Enable Ring Morph", CHECKMARK(module->ringMorph));
        ringMorphItem->module = module;
        menu->addChild(ringMorphItem);

        menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Interaction"));
        
		menu->addChild(construct<ResetSceneMenuItem<Algomorph4>>(&MenuItem::text, "Reset Scene", &MenuItem::rightText, RIGHT_ARROW, &ResetSceneMenuItem<Algomorph4>::module, module));
        
        CCWScenesItem *ccwScenesItem = createMenuItem<CCWScenesItem>("Trigger input - reverse sequence", CHECKMARK(!module->ccwSceneSelection));
        ccwScenesItem->module = module;
        menu->addChild(ccwScenesItem);

        ExitConfigItem *exitConfigItem = createMenuItem<ExitConfigItem>("Exit Edit Mode after Connection", CHECKMARK(module->exitConfigOnConnect));
        exitConfigItem->module = module;
        menu->addChild(exitConfigItem);

        menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Visual"));
        
        VULightsItem *vuLightsItem = createMenuItem<VULightsItem>("Disable VU lighting", CHECKMARK(!module->vuLights));
        vuLightsItem->module = module;
        menu->addChild(vuLightsItem);
        
        GlowingInkItem *glowingInkItem = createMenuItem<GlowingInkItem>("Enable glowing panel ink", CHECKMARK(module->glowingInk));
        glowingInkItem->module = module;
        menu->addChild(glowingInkItem);

        menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Mode"));

        AllowHorizontalItem *allowHorizontalItem = createMenuItem<AllowHorizontalItem>("A", CHECKMARK(!module->horizontalAllowed));
        allowHorizontalItem->module = module;
        menu->addChild(allowHorizontalItem);

        DisallowHorizontalItem *disallowHorizontalItem = createMenuItem<DisallowHorizontalItem>("B", CHECKMARK(module->horizontalAllowed));
        disallowHorizontalItem->module = module;
        menu->addChild(disallowHorizontalItem);

        DebugItem *debugItem = createMenuItem<DebugItem>("The system is down", CHECKMARK(module->debug));
        debugItem->module = module;
        menu->addChild(debugItem);
    }

    void step() override {
		if (module) {
			if (dynamic_cast<Algomorph4*>(module)->glowingInk) {
                if (!ink->sw->svg)
                    ink->sw->svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/GlowingInk.svg"));
            }
            else {
                if (ink->sw->svg)
                    ink->sw->svg = NULL;
            }
		}
		ModuleWidget::step();
	}
};


Model* modelAlgomorph4 = createModel<Algomorph4, Algomorph4Widget>("Algomorph4");
