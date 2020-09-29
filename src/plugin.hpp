#pragma once
#include <rack.hpp>
#include <bitset>
using namespace rack;
// Declare the Plugin, defined in plugin.cpp
extern Plugin* pluginInstance;
// Declare each Model, defined in each module source file
// extern Model* modelMyModule;
extern Model* modelAlgomorph4;

static const NVGcolor DLXLightPurple = nvgRGB(139, 112, 162);
static const NVGcolor DLXRed = nvgRGB(0xae, 0x34, 0x58);
static const NVGcolor DLXYellow = nvgRGB(0xa9, 0xa9, 0x83);

struct DLXKnob : RoundKnob {
	DLXKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DLXKnob.svg")));
		// shadow->opacity = 0.f;
	}
};

struct DLXPortPoly : SvgPort {
	DLXPortPoly() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DLXPort16.svg")));
		// shadow->opacity = 0.f;
	}
};

struct DLXPortPolyOut : SvgPort {
	DLXPortPolyOut() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DLXPort16b.svg")));
		// shadow->opacity = 0.f;
	}
};

struct DLXPortG : SvgPort {
	DLXPortG() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DLXPortG.svg")));
		// shadow->opacity = 0.f;
	}
};

struct DLXEditButton : rack::app::SvgSwitch {
	int state = 0;

	DLXEditButton() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DLX_Button_0b.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DLX_Button_1b.svg")));
	}
};

template <typename TBase = GrayModuleLightWidget>
struct TDlxPurpleLight : TBase {
	TDlxPurpleLight() {
		this->addBaseColor(DLXLightPurple);
	}
};
typedef TDlxPurpleLight<> DLXPurpleLight;

template <typename TBase = GrayModuleLightWidget>
struct TDlxRedLight : TBase {
	TDlxRedLight() {
		this->addBaseColor(DLXRed);
	}
};
typedef TDlxRedLight<> DLXRedLight;

template <typename TBase = GrayModuleLightWidget>
struct TDlxYellowLight : TBase {
	TDlxYellowLight() {
		this->addBaseColor(DLXYellow);
	}
};
typedef TDlxYellowLight<> DLXYellowLight;

template <typename TBase = GrayModuleLightWidget>
struct TDlxMultiLight : TBase {
	TDlxMultiLight() {
		this->addBaseColor(DLXLightPurple);
		this->addBaseColor(DLXYellow);
		this->addBaseColor(DLXRed);
	}
};
typedef TDlxMultiLight<> DLXMultiLight;

template <typename TBase = GrayModuleLightWidget>
struct TLineLight : TBase {
	bool flipped = false;
	TLineLight(Vec a, Vec b) {
		if (a.y > b.y) {
			flipped = true;
			this->box.pos.y = b.y;
		}
		this->box.size = Vec(b.x - a.x, std::max(a.y, b.y) - std::min(a.y, b.y));
	}
	void drawLight(const widget::Widget::DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0.f, 0.f);
		nvgLineTo(args.vg, this->box.size.x, flipped ? -this->box.size.y : this->box.size.y);
		if (this->color.a > 0.0) {
			nvgStrokeWidth(args.vg, 1.f);
			nvgStrokeColor(args.vg, this->color);
			nvgStroke(args.vg);
		}
	}
	void drawHalo(const widget::Widget::DrawArgs& args) override {
		/* float radius = std::min(this->box.size.x, this->box.size.y) / 2.0;
		float oradius = 3.0 * radius;

		nvgBeginPath(args.vg);
		if (flipped)
			nvgRect(args.vg, radius - oradius, radius - oradius - this->box.size.y, 2 * oradius, 2 * oradius);
		else
			nvgRect(args.vg, radius - oradius, radius - oradius, 2 * oradius, 2 * oradius);

		NVGpaint paint;
		NVGcolor icol = color::mult(this->color, 0.07);
		NVGcolor ocol = nvgRGB(0, 0, 0);
		if (flipped)
			paint = nvgRadialGradient(args.vg, radius, radius - this->box.size.y, radius, oradius, icol, ocol);
		else
			paint = nvgRadialGradient(args.vg, radius, radius, radius, oradius, icol, ocol);
		nvgFillPaint(args.vg, paint);
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFill(args.vg); */
	}
};
typedef TLineLight<> LineLight;

template <typename TBase = DLXPurpleLight>
TLineLight<TBase>* createLineLight(Vec a, Vec b, engine::Module* module, int firstLightId) {
	TLineLight<TBase>* o = new TLineLight<TBase>(a, b);
	o->box.pos = a;
	o->module = module;
	o->firstLightId = firstLightId;
	return o;
}

template <typename TBase = GrayModuleLightWidget>
struct TRingLight : TBase {
	float radius = 1.f;
	TRingLight(float r) {
		radius = r;
	}
	void drawLight(const widget::Widget::DrawArgs& args) override {
			// Adapted from LightWidget::drawLight, with no fill
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, radius, radius, radius);
			nvgStrokeWidth(args.vg, 1);

			// Background
			if (this->bgColor.a > 0.0) {
				nvgStrokeColor(args.vg, this->bgColor);
				nvgStroke(args.vg);
			}

			// Foreground
			if (this->color.a > 0.0) {
				nvgStrokeColor(args.vg, this->color);
				nvgStroke(args.vg);
			}

			// Border
			if (this->borderColor.a > 0.0) {
				nvgStrokeWidth(args.vg, 0.5);
				nvgStrokeColor(args.vg, this->borderColor);
				nvgStroke(args.vg);
			}
	}
	void drawHalo(const widget::Widget::DrawArgs& args) override {	}
};
typedef TRingLight<> RingLight;

template <typename TBase = DLXPurpleLight>
TRingLight<TBase>* createRingLight(Vec pos, float r, engine::Module* module, int firstLightId) {
	TRingLight<TBase>* o = new TRingLight<TBase>(r);
	o->box.pos = pos;
	o->module = module;
	o->firstLightId = firstLightId;
	return o;
}

template <typename TBase = DLXPurpleLight>
TRingLight<TBase>* createRingLightCentered(Vec pos, float r, engine::Module* module, int firstLightId) {
	TRingLight<TBase>* o = createRingLight<TBase>(pos, r, module, firstLightId);
	o->box.pos.x -= r;
	o->box.pos.y -= r;
	return o;
}

struct bitsetCompare {
    bool operator() (const std::bitset<16> &b1, const std::bitset<16> &b2) const {
        return b1.to_ulong() < b2.to_ulong();
	}
};