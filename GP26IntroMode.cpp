#include "GP26IntroMode.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <algorithm>
#include <chrono>
#include <array>

static std::unique_ptr< Sound::Sample > music_sample;

GP26IntroMode::GP26IntroMode(std::function< void() > const &on_finished_) : on_finished(on_finished_) {

	//this little synth thing based on:
	// https://github.com/15-466/15-466-f22-intro/blob/main/GP22IntroMode.cpp

	{ // ------ sound ------
		std::vector< float > data(10 * 48000, 0.0f);

		//triangle wave (lightly phase-modulated):
		[[maybe_unused]] auto triangle = [](float t) -> float {
			float pm = std::sin(t * 0.999f * 2.0f * float(M_PI));
			t += 0.1f * pm;
			t += 0.25f;
			return std::abs(4.0f * (t - std::floor(t)) - 2.0f) - 1.0f;
		};
		//sine wave (lightly phase-modulated + sub-bass):
		[[maybe_unused]] auto sine = [](float t) -> float {
			float pm = std::sin(t * 1.01f * 2.0f * float(M_PI));
			float sub = std::sin(0.5f * t * 2.0f * float(M_PI));
			return 0.5f * std::sin((t + 0.3f * pm) * 2.0f * float(M_PI)) + 0.5f * sub;
		};
		//square wave (lightly pulse-width-modulated):
		[[maybe_unused]] auto square = [](float t) -> float {
			float w = 0.5f + 0.1f * std::sin(t * 0.1f);
			return 0.5f * (t - std::floor(t) > w ? 1.0f : -1.0f);
		};
		//sawtooth wave:
		[[maybe_unused]] auto saw = [](float t) -> float {
			return (t - std::floor(t)) - 0.5f;
		};
		//"super saw" (stack of detuned saws):
		//loosely based on: https://blog.faderpro.com/techniques/supersaw-how-make-iconic-sound/
		[[maybe_unused]] auto supersaw = [&saw](float t) -> float {
			float w = 0.0f;
			w += saw(t);
			float f = 1.0f;
			for (uint32_t i = 1; i < 7; i += 2) {
				f *= 1.0f + 0.21f * (std::pow(2.0f, 1.0f / 12.0f) - 1.0f); //detune by 21% of one semitone
				w += saw(f * t);
				w += saw(f / t);
			}
			t *= 0.25f;
			constexpr float v = 0.7f;
			w += v * saw(t);
			f = 1.0f;
			for (uint32_t i = 1; i < 3; ++i) {
				f *= 1.0f + 0.07f * (std::pow(2.0f, 1.0f / 12.0f) - 1.0f); //detune by 7% of one semitone
				w += v * saw(f * t);
				w += v * saw(f / t);
			}

			return w;
		};
		//FM bell-like sound(?):
		//loosely based on: https://www.pluginboutique.com/articles/1873-FM-Synthesis-Cookbook-Five-Classic-FM-Sounds-and-How-They-Work
		[[maybe_unused]] auto bell = [](float t) -> float {
			float ratio = 5.71f;
			float mod = std::sin((ratio * t) * float(M_PI) * 2.0f) * 4.0f;
			mod *= std::exp2(-t / 30.0f);
			float w = std::sin((t + mod) * float(M_PI) * 2.0f);
			return 0.5f * w;
		};

		struct ADSR {
			float a; //attack
			float d; //decay
			float s; //sustain
			float r; //release
		};

		//play synth at given time for given length at given frequency with given wave:
		auto tone = [&](float start, float len, float hz, float vol, std::function< float(float) > const &wave, ADSR const &adsr){
			int32_t begin = int32_t(start * 48000);
			int32_t end = begin + int32_t((len + adsr.r) * 48000);
			end = std::min(end, int32_t(data.size()));

			for (int32_t sample = begin; sample < end; ++sample) {
				float t = (sample - begin + 0.5f) / 48000.0f;

				//envelope value:
				float env;
				if (t < adsr.a) env = t / adsr.a;
				else if (t < adsr.a + adsr.d) env = ((t - adsr.a) / adsr.d) * (adsr.s - 1.0f) + 1.0f;
				else if (t <= len) env = adsr.s;
				else env = std::min(1.0f, (t - len) / adsr.r) * (0.0f - adsr.s) + adsr.s;

				//simple, single-oscillator synth:
				float osc1 = wave(t * hz);

				data[sample] += vol * env * osc1;
			}
		};

		//midi note number to frequency (based on A4 being 440hz)
		[[maybe_unused]] auto midi2hz = [](float midi) -> float {
			return 440.0f * std::exp2( (midi - 69.0f) / 12.0f );
		};

		[[maybe_unused]] auto C = [](int32_t oct) { return 12.0f + 12.0f * oct; };
		[[maybe_unused]] auto Cs= [](int32_t oct) { return 13.0f + 12.0f * oct; };
		[[maybe_unused]] auto D = [](int32_t oct) { return 14.0f + 12.0f * oct; };
		[[maybe_unused]] auto Ds= [](int32_t oct) { return 15.0f + 12.0f * oct; };
		[[maybe_unused]] auto E = [](int32_t oct) { return 16.0f + 12.0f * oct; };
		[[maybe_unused]] auto F = [](int32_t oct) { return 17.0f + 12.0f * oct; };
		[[maybe_unused]] auto Fs= [](int32_t oct) { return 18.0f + 12.0f * oct; };
		[[maybe_unused]] auto G = [](int32_t oct) { return 19.0f + 12.0f * oct; };
		[[maybe_unused]] auto Gs= [](int32_t oct) { return 20.0f + 12.0f * oct; };
		[[maybe_unused]] auto A = [](int32_t oct) { return 21.0f + 12.0f * oct; };
		[[maybe_unused]] auto As= [](int32_t oct) { return 22.0f + 12.0f * oct; };
		[[maybe_unused]] auto B = [](int32_t oct) { return 23.0f + 12.0f * oct; };


		[[maybe_unused]] ADSR env0{
			.a = 0.15f,
			.d = 3.5f,
			.s = 0.0f,
			.r = 0.0f,
		};

		[[maybe_unused]] ADSR env1{
			.a = 0.5f,
			.d = 0.0f,
			.s = 1.0f,
			.r = 1.0f,
		};

		[[maybe_unused]] ADSR env2{
			.a = 1.5f,
			.d = 0.1f,
			.s = 1.0f,
			.r = 1.5f,
		};

		[[maybe_unused]] ADSR env3{
			.a = 2.5f,
			.d = 2.5f,
			.s = 0.0f,
			.r = 0.0f,
		};

		[[maybe_unused]] ADSR envR{
			.a = 0.5f,
			.d = 4.5f,
			.s = 0.0f,
			.r = 0.0f,
		};

		tone(0.0f, 1.0f, midi2hz(Cs(1)), 0.2f, square, envR);

		tone(0.5f, 5.0f, midi2hz(F(0)), 1.0f, bell, env0);
		tone(0.5f, 5.0f, midi2hz(Gs(3)), 1.0f, bell, env0);
		//tone(0.0f, 5.0f, midi2hz(Gs(6)), 0.5f, bell, env0);

		tone(0.75f, 3.0f, midi2hz(Gs(4)), 0.5f, supersaw, env1);

		tone(2.5f, 3.5f, midi2hz(Cs(4)), 0.5f, supersaw, env2);


		{ //gently re-center output (remove dc):
			float smoothed = 0.0f;
			for (float &s : data) {
				smoothed += 1e-4f * (s - smoothed);
				s -= smoothed;
			}
		}


		{ //run a basic 'digital reverb' over stuff:
			//use a delay line with a few different taps and feedbacks:
			std::array< float, 2 * 48000 > delay;
			for (auto &s : delay) s = 0.0f; //clear delay line
			uint32_t head = 0;
			auto tap = [&](float offset) -> float {
				return delay[(head + int32_t(delay.size()) - int32_t(std::floor(offset * 48000.0f))) % delay.size()];
			};
			float smoothed = 0.0f;
			for (float &s : data) {
				float wet = 1.0f * s + (
					+ 6.0f * tap(0.43f)
					+ 6.0f * tap(0.21f)
					+ 3.0f * tap(0.13f)
					+ 1.0f * tap(0.09f)
					) / 16.0f;
				smoothed += 0.95f * (wet - smoothed); //smooth off high frequencies before writing reverb buffer
				delay[head] = smoothed;
				head = (head + 1) % delay.size();
				s = 0.25f * tap(0.25f) + 1.0f * s;
			}
		}

		{ //do a low-pass filter on output:
			float C = 0.6f;
			float smoothed = 0.0f;
			for (float &s : data) {
				smoothed += C * (s - smoothed);
				s = smoothed;
			}
			smoothed = 0.0f;
			for (float &s : data) {
				smoothed += C * (s - smoothed);
				s = smoothed;
			}
		}

		{ //fade out the last bit, just in case:
			constexpr uint32_t fade = 2*48000;
			for (uint32_t i = 0; i < fade; ++i) {
				float amt = (i + 0.5f) / float(fade);
				data[data.size()-1-i] *= amt;
			}
		}

		/*{ //DEBUG:
			std::ofstream dump("music-dump.f32", std::ios::binary);
			dump.write(reinterpret_cast< const char * >(data.data()), data.size() * 4);
		}*/


		if (!music_sample) { //<-- when playing repeatedly for testing, want to avoid deallocating sample
			music_sample = std::make_unique< Sound::Sample >(data);
		}

		music = Sound::play(*music_sample);
	}

	//--------------------------------------------

	/*

	trace.program = gl_compile_program(
		"#version 330\n"
		"out vec2 Clip;\n"
		"void main() {\n"
		"	vec2 pos = vec2((gl_VertexID & 1) * 4.0 - 1.0, (gl_VertexID & 2) * 2.0 - 1.0);\n"
		"	gl_Position = vec4(pos, 0.0, 1.0 );\n"
		"	Clip = pos;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"#line " STR(__LINE__) "\n"
		"uniform mat3 ORIGIN_FROM_CLIP;\n"
		"uniform mat3 DIRECTION_FROM_CLIP;\n"
		"uniform float LIGHTING;\n"
		"uniform vec3 LIGHT;\n"
		"in vec2 Clip;\n"
		"out vec4 fragColor;\n"
		"int evals = 0;\n"
		"//based on code from Inigo Quilez: https://www.shadertoy.com/view/tl23RK \n"
		"// distance to capped torus. sc is the sine and cosine of half of the total angle of the torus.\n"
		"float sdCappedTorus(in vec3 p, in vec2 sc, in float ra, in float rb) {\n"
		"	p.x = abs(p.x);\n"
		"	float k = (sc.y * p.x > sc.x * p.y) ? dot(p.xy, sc) : length(p.xy);\n"
		"	return sqrt( dot(p,p) + ra*ra - 2.0 * ra * k ) - rb;\n"
		"}\n"
		"float sdTorus(in vec3 p, in float ra, in float rb) {\n"
		"	vec2 q = vec2(length(p.xy) - ra, p.z);\n"
		"	return length(q) - rb;\n"
		"}\n"
		"float sdCapsule( in vec3 p, in vec3 a, in vec3 b, float r ) {\n"
		"	vec3 pa = p - a, ba = b - a;\n"
		"	float h = clamp( dot(pa,ba) / dot(ba,ba), 0.0, 1.0 );\n"
		"	return length( pa - ba*h ) - r;\n"
		"}\n"
		"float sdBox( in vec3 p, in vec3 r ) {\n"
		"	vec3 q = abs(p) - r;\n"
		"	return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);\n"
		"}\n"
		"float sdGP26(in vec3 p, float R) {\n"
		"	float d = 1e6;\n"
		"	{ // G \n"
		"		vec3 pl = p - vec3(-60.0, 60.0, 0.0);\n"
		"		const float r = radians(111.07);\n"
		"		pl.xy = pl.xy * mat2(\n"
		"			cos(r), sin(r),\n"
		"			-sin(r), cos(r)\n"
		"		);\n"
		"		const float ha = radians(158.93);\n"
		"		d = min(d, sdCappedTorus(pl, vec2(sin(ha), cos(ha)), 45.0, R));\n"
		"		d = min(d, sdCapsule(p, vec3(-60.0, 60.0, 0.0), vec3(-15.0, 60.0, 0.0), R));\n"
		"	}\n"
		"	{ // P \n"
		"		d = min(d, sdTorus(p - vec3( 60.0, 60.0, 0.0), 45.0, R));\n"
		"		d = min(d, sdCapsule(p, vec3(15.0, 60.0, 0.0), vec3(15.0,0.0, 0.0), R));\n"
		"	}\n"
		"	{ // 2 \n"
		"		vec3 pl = p - vec3(-60.0,-60.0, 0.0);\n"
		"		const float r = radians(-45.0);\n"
		"		pl.xy = pl.xy * mat2(\n"
		"			cos(r), sin(r),\n"
		"			-sin(r), cos(r)\n"
		"		);\n"
		"		const float ha = radians(135.0);\n"
		"		d = min(d, sdCappedTorus(pl, vec2(sin(ha), cos(ha)), 45.0, R));\n"
		"		d = min(d, sdCapsule(p, vec3(-105.0,-105.0, 0.0), vec3(0.0,-105.0, 0.0), R));\n"
		"	}\n"
		"	{ // 5 \n"
		"		vec3 pl = p - vec3( 60.0,-60.0, 0.0);\n"
		"		const float r = radians(45.0);\n"
		"		pl.xy = pl.xy * mat2(\n"
		"			cos(r), sin(r),\n"
		"			-sin(r), cos(r)\n"
		"		);\n"
		"		const float ha = radians(45.0);\n"
		"		d = min(d, sdCappedTorus(pl, vec2(sin(ha), cos(ha)), 45.0, R));\n"
		"		d = min(d, sdCappedTorus(-pl, vec2(sin(ha), cos(ha)), 45.0, R));\n"
		"		d = min(d, sdCapsule(p, vec3(15.0,-60.0, 0.0), vec3(105.0,-60.0, 0.0), R));\n"
		"		d = min(d, sdCapsule(p, vec3(60.0,-15.0, 0.0), vec3(105.0,-15.0, 0.0), R));\n"
		"		d = min(d, sdCapsule(p, vec3(30.0,-105.0, 0.0), vec3(60.0,-105.0, 0.0), R));\n"
		"	}\n"
		"	return d;\n"
		"}\n"
		"float sdTrack(in vec3 p) {\n"
		"	float d = 1000.0;\n"
		"	//flat platform:\n"
		"	d = min(d, max(abs(p.z- -7.5)-7.5, sdGP26(vec3(p.xy,0.0),15.0)) );\n"
		"	//carve track:\n"
		"	d = max(d, -sdGP26(p - vec3(0.0, 0.0, 15.0), 20.0));\n"
		"	return d;\n"
		"}\n"
		"float sdMarbles(in vec3 p) {\n"
		"	float d = 1000.0;\n"
		"	d = min(d, length(p - vec3(-60.0, 60.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(-26.632, 90.192, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(15.0, 0.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(-105.0,-60.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(-105.0,-105.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(0.0,-105.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(30.0,-105.0, 10.0)) - 15.0);\n"
		"	d = min(d, length(p - vec3(105.0,-15.0, 10.0)) - 15.0);\n"
		"	return d;\n"
		"}\n"
		"float sdScene(in vec3 p) {\n"
		"	evals += 1;\n"
		"	float d = 1000.0;\n"
		"	d = min(d, sdTrack( p ));\n"
		"	//box:\n"
		"	d = min(d, sdBox( p - vec3(0.0, 0.0,-10.5), vec3(105.0, 105.0, 4.5)));\n"
		"	//marbles:\n"
		"	//d = min(d, sdMarbles( p ));\n"
		"	return d;\n"
		"}\n"
		"//after iq: https://iquilezles.org/articles/intersectors/\n"
		"void rtSphere( in vec3 ro, in vec3 rd, in vec3 ce, in float ra, inout float t, inout vec3 normal) {\n"
		"	vec3 oc = ro - ce;\n"
		"	float b = dot( oc, rd );\n"
		"	float c = dot( oc, oc ) - ra*ra;\n"
		"	float h = b*b - c;\n"
		"	if (h > 0.0) {\n"
		"		h = sqrt(h);\n"
		"		float t_min = -b - h;\n"
		"		if (t_min > 0.0 && t_min < t) {\n"
		"			t = t_min;\n"
		"			normal = ro + rd * t - ce;\n"
		"		}\n"
		"	}\n"
		"}\n"
		"bool rtSphereAny( in vec3 ro, in vec3 rd, in vec3 ce, in float ra) {\n"
		"	vec3 oc = ro - ce;\n"
		"	float b = dot( oc, rd );\n"
		"	float c = dot( oc, oc ) - ra*ra;\n"
		"	float h = b*b - c;\n"
		"	if (h > 0.0) {\n"
		"		h = sqrt(h);\n"
		"		float t_max = -b + h;\n"
		"		return t_max > 0.0;\n"
		"	}\n"
		"}\n"
		"void rtMarbles(in vec3 ro, in vec3 rd, inout float t, inout vec3 normal) {\n"
		"	rtSphere(ro, rd, vec3(-60.0, 60.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(-26.632, 90.192, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(15.0, 0.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(-105.0,-60.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(-105.0,-105.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(0.0,-105.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(30.0,-105.0, 10.0), 15.0, t, normal);\n"
		"	rtSphere(ro, rd, vec3(105.0,-15.0, 10.0), 15.0, t, normal);\n"
		"}\n"
		"bool rtMarblesAny(in vec3 ro, in vec3 rd) {\n"
		"	return\n"
		"	rtSphereAny(ro, rd, vec3(-60.0, 60.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(-26.632, 90.192, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(15.0, 0.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(-105.0,-60.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(-105.0,-105.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(0.0,-105.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(30.0,-105.0, 10.0), 15.0)\n"
		"	|| rtSphereAny(ro, rd, vec3(105.0,-15.0, 10.0), 15.0);\n"
		"}\n"
		"void rtSlab(in vec3 ro, in vec3 rd, inout float tmin, inout float tmax) {\n"
		"	const vec3 MIN = vec3(-120.0, -120.0, -15.0);\n"
		"	const vec3 MAX = vec3( 120.0,  120.0, 0.0);\n"
		"	tmin = max(tmin, ((rd.x < 0.0 ? MAX.x : MIN.x) - ro.x) / rd.x);\n"
		"	tmax = min(tmax, ((rd.x < 0.0 ? MIN.x : MAX.x) - ro.x) / rd.x);\n"
		"	tmin = max(tmin, ((rd.y < 0.0 ? MAX.y : MIN.y) - ro.y) / rd.y);\n"
		"	tmax = min(tmax, ((rd.y < 0.0 ? MIN.y : MAX.y) - ro.y) / rd.y);\n"
		"	tmin = max(tmin, ((rd.z < 0.0 ? MAX.z : MIN.z) - ro.z) / rd.z);\n"
		"	tmax = min(tmax, ((rd.z < 0.0 ? MIN.z : MAX.z) - ro.z) / rd.z);\n"
		"}\n"
		"vec3 Albedo(in vec3 p) {\n"
		"	float d = 1000.0;\n"
		"	vec3 c = vec3(1.0, 0.0, 1.0);\n"
		"	float dt = sdTrack( p );\n"
		"	if (dt < d) {\n"
		"		d = dt;\n"
		"		c = vec3( 0.922, 0.309, 0.150 );\n"
		"	}\n"
		"	float db = sdBox( p - vec3(0.0, 0.0,-10.5), vec3(105.0, 105.0, 4.5));\n"
		"	if (db < d) {\n"
		"		d = db;\n"
		"		c = vec3( 0.687, 0.939, 0.565 );\n"
		"	}\n"
		"	return c;\n"
		"}\n"
		"//from https://iquilezles.org/articles/normalsSDF/ :\n"
		"vec3 sdNormal( in vec3 p ) {\n"
		"	const float h = 0.0005;\n"
		"	const vec2 k = vec2(1.0,-1.0)*0.57735;\n"
		"	return normalize(\n"
		"		k.xyy*sdScene( p + k.xyy*h ) +\n"
		"		k.yyx*sdScene( p + k.yyx*h ) +\n"
		"		k.yxy*sdScene( p + k.yxy*h ) +\n"
		"		k.xxx*sdScene( p + k.xxx*h )\n"
		"	);\n"
		"}\n"
		"void main() {\n"
		"	vec3 Origin = ORIGIN_FROM_CLIP * vec3(Clip, 1.0);\n"
		"	vec3 Direction = DIRECTION_FROM_CLIP * vec3(Clip, 1.0);\n"
		"	vec3 l = normalize(LIGHT);\n"
		"	vec3 rd = normalize(Direction);\n"
		"	vec3 ro = Origin;\n"
		"	const float FAR = 500.0;\n"
		"	//intersection info:\n"
		"	float t = FAR;\n"
		"	vec3 normal = vec3(0.0, 0.0, 0.0);\n"
		"	vec3 albedo = vec3(1.0, 0.0, 1.0);\n"
		"	{ //check vs marbles:\n"
		"		rtMarbles(ro, rd, t, normal);\n"
		"		if (t != FAR) {\n"
		"			albedo = vec3( 1.0, 0.604, 0.0 );\n"
		"		}\n"
		"	}\n"
		"	{ //check vs slab:\n"
		"		float tmin = 0.0;\n"
		"		float tmax = t;\n"
		"		rtSlab(ro, rd, tmin, tmax);\n" //limit to the range where the slab is
		"		if (tmin < tmax) {\n"
		"			for ( int i = 0; i < 256; ++i) {\n"
		"				vec3 pos = ro + tmin*rd;\n"
		"				float h = sdScene(pos);\n"
		"				if (h < 0.0001 || tmin > tmax) break;\n"
		"				tmin += h;\n"
		"			}\n"
		"			if (tmin < tmax) {\n"
		"				t = tmin;\n"
		"				vec3 p = ro + t * rd;\n"
		"				normal = sdNormal(p);\n"
		"				albedo = Albedo(p);\n"
		"			}\n"
		"		}\n"
		"	}\n"
		"	if (t < FAR) {\n"
		"		//hit something in the scene. Do lighting:\n"
		"		vec3 n = normalize(normal);\n"
		"		vec3 e = vec3(0.2, 0.2, 0.22) * (0.5 + 0.5 * dot(n, vec3(0,0,1))); //sky\n"
		"		vec3 so = ro + t * rd + n * 0.1;\n"
		"		vec3 sd = l;\n"
		"		float s = FAR;\n"
		"		vec3 unused;\n"
		"		if (rtMarblesAny(so, sd)) s = 0;\n"
		"		if (s == FAR) {\n"
		"			//if no marble hits, look for a hit in the slab:\n"
		"			float smin = 0.0;\n"
		"			float smax = s;\n"
		"			rtSlab(so, sd, smin, smax);\n" //limit to the range where the slab is
		"			if (smin < smax) {\n"
		"				for ( int i = 0; i < 64; ++i) {\n"
		"					vec3 pos = so + smin*sd;\n"
		"					float h = sdScene(pos);\n"
		"					if (h < 0.0001 || smin > smax) break;\n"
		"					smin += h;\n"
		"				}\n"
		"				if (smin < smax) {\n"
		"					s = smin;\n"
		"				}\n"
		"			}\n"
		"		}\n"
		"		if (s == FAR) {\n"
		"			e += 1.2 * vec3(1.0, 1.0, 0.95) * max(0, dot(n, l)); //sun\n"
		"		}\n"
		"		fragColor = vec4(mix(vec3(1.0), e, LIGHTING) * albedo, 1.0);\n"
		"	} else {\n"
		"		//horizon/sky:\n"
		"		fragColor = vec4(mix( vec3(1.0, 1.0, 1.0), vec3(0.0, 0.0, 0.0), clamp((rd.z + 0.1) / 0.2, 0.0, 1.0)), 1.0);\n"
		"	}\n"
		"}\n"
	);

	//look up the locations of uniforms:
	trace.ORIGIN_FROM_CLIP_mat3 = glGetUniformLocation(trace.program, "ORIGIN_FROM_CLIP");
	trace.DIRECTION_FROM_CLIP_mat3 = glGetUniformLocation(trace.program, "DIRECTION_FROM_CLIP");
	trace.LIGHTING_float = glGetUniformLocation(trace.program, "LIGHTING");
	trace.LIGHT_vec3 = glGetUniformLocation(trace.program, "LIGHT");

	glGenVertexArrays(1, &empty_vao);
	glBindVertexArray(empty_vao);
	//nothing to configure
	glBindVertexArray(0);

	GL_ERRORS();

	*/

}

GP26IntroMode::~GP26IntroMode() {
}

void GP26IntroMode::benchmark() {
	std::cout << "benchmark" << std::endl;

	glFlush();
	glFinish();

	int w,h;
	SDL_GetWindowSizeInPixels(Mode::window, &w, &h);
	glm::uvec2 drawable_size = glm::uvec2(w, h);
	glViewport(0, 0, drawable_size.x, drawable_size.y);


	for (uint32_t iter = 0; iter < 4; ++iter) {
		double total = 0.0;
		double min = std::numeric_limits< double >::infinity();
		double max =-std::numeric_limits< double >::infinity();
		uint32_t frames = 10 * 30; //not as complete but faster to wait for
		for (uint32_t f = 0; f < frames; ++f) {
			auto before = std::chrono::high_resolution_clock::now();
			time = f / float(frames) * 10.0f;
			draw(drawable_size);
			glFlush();
			glFinish();
			auto after = std::chrono::high_resolution_clock::now();
			double ms = std::chrono::duration< double >(after - before).count() * 1000.0;
			total += ms;
			min = std::min(min, ms);
			max = std::max(max, ms);
		}
		std::cout << min << " " << total / double(frames) << " " << max << std::endl;
	}
}

bool GP26IntroMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_PRINTSCREEN) {
			//ignore the "printscreen" key:
			return false;
		} else {
			finish();
			return true;
		}
	}
	return false;
}

void GP26IntroMode::update(float elapsed) {
	time += elapsed;
	if (time > 10.0f) {
		finish();
		return;
	}
}

void GP26IntroMode::draw(glm::uvec2 const &drawable_size) {

	//Clear background:
	glClearColor(0.2f, 0.7f, 0.3f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	/*
	glUseProgram(trace.program);
	glUniformMatrix3fv(trace.DIRECTION_FROM_CLIP_mat3, 1, GL_FALSE, glm::value_ptr(direction_from_clip));
	glUniformMatrix3fv(trace.ORIGIN_FROM_CLIP_mat3, 1, GL_FALSE, glm::value_ptr(origin_from_clip));
	float lighting_fade = std::clamp((time - 1.5f) / 1.5f, 0.0f, 1.0f);
	glUniform1f(trace.LIGHTING_float, lighting_fade);
	glm::vec3 light = glm::normalize(glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(2.0f, 1.0f, 0.5f), lighting_fade));
	glUniform3fv(trace.LIGHT_vec3, 1, glm::value_ptr(light));

	glBindVertexArray(empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	*/

	glUseProgram(0);

	GL_ERRORS();
}


void GP26IntroMode::finish() {
	if (loop_forever) {
		if (music) music->set_volume(0.0f, 1.0f / 10.0f);
		music = Sound::play(*music_sample);
		time = 0.0f;
	} else {
		music->set_volume(0.0f, 1.0f / 10.0f);
		std::function< void() > on_finished_ = on_finished;
		Mode::set_current(nullptr);
		if (on_finished_) on_finished_();
	}
}
