#include "GP26IntroMode.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <algorithm>
#include <chrono>
#include <array>

//from GP22IntroMode:
static void stroke(std::vector< GP26IntroMode::Vertex > *attribs_,
		std::vector< glm::vec2 > const &path, std::vector< float > const &param, float r,
		glm::u8vec4 col_begin, std::vector< std::pair< float, glm::u8vec4 > > const &col_mid, glm::u8vec4 col_end) {
	assert(attribs_);
	auto &attribs = *attribs_;

	assert(param.size() == path.size());

	if (path.size() < 2) return;

	glm::vec2 min = glm::vec2(std::numeric_limits< float >::infinity());
	glm::vec2 max = glm::vec2(-std::numeric_limits< float >::infinity());

	std::vector< std::pair< float, glm::u8vec4 > > param_col;
	param_col.reserve(col_mid.size() + 2);
	param_col.emplace_back(0.0f, col_begin);
	param_col.insert(param_col.end(), col_mid.begin(), col_mid.end());
	param_col.emplace_back(param.back(), col_end);

	uint32_t pci = 0;

	bool first = true;
	auto emit = [&](glm::vec2 const &vert, float p) {
		while (pci + 1 < param_col.size() && param_col[pci+1].first <= p) ++pci;

		float amt = (p - param_col[pci].first) / (param_col[pci+1].first - param_col[pci].first);
		glm::u8vec4 col = glm::u8vec4(
			glm::mix(glm::vec4(param_col[pci].second), glm::vec4(param_col[pci+1].second), amt)
		);

		min = glm::min(min, vert);
		max = glm::max(max, vert);
		if (first && !attribs.empty()) attribs.emplace_back(attribs.back());
		attribs.emplace_back(vert, col);
		if (first && attribs.size() != 1) attribs.emplace_back(attribs.back());
		first = false;
	};

	//mitered line strip:
	glm::vec2 prev_along;
	{
		glm::vec2 along = glm::normalize(path[1] - path[0]);
		glm::vec2 perp = glm::vec2(-along.y, along.x);
		emit(path[0] - r * perp, param[0]);
		emit(path[0] + r * perp, param[0]);
		prev_along = along;
	}
	for (uint32_t i = 1; i + 1 < path.size(); ++i) {
		glm::vec2 along = glm::normalize(path[i+1] - path[i]);
		glm::vec2 perp = glm::vec2(-along.y, along.x);

		glm::vec2 avg_along = glm::normalize(along + prev_along);
		glm::vec2 avg_perp = glm::vec2(-avg_along.y, avg_along.x);

		float r_ = r / glm::max(glm::dot(avg_perp, perp), 0.5f);

		emit(path[i] - r_ * avg_perp, param[i]);
		emit(path[i] + r_ * avg_perp, param[i]);

		prev_along = along;
	}
	{
		glm::vec2 along = glm::normalize(path.back() - path[path.size()-2]);
		glm::vec2 perp = glm::vec2(-along.y, along.x);
		emit(path.back() - r * perp, param.back());
		emit(path.back() + r * perp, param.back());
	}
}


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

		// ------ shader ------
	//(based on ColorProgram.cpp)
	color.program = gl_compile_program(
		"#version 330\n"
		"uniform mat4 CLIP_FROM_OBJECT;\n"
		"in vec4 Position;\n"
		"in vec4 Color;\n"
		"out vec4 color;\n"
		"void main() {\n"
		"	gl_Position = CLIP_FROM_OBJECT * Position;\n"
		"	color = Color;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"in vec4 color;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	fragColor = color;\n"
		"}\n"
	);

	//look up the locations of vertex attributes:
	GLuint Position_vec4 = glGetAttribLocation(color.program, "Position");
	GLuint Color_vec4 = glGetAttribLocation(color.program, "Color");

	//look up the locations of uniforms:
	color.CLIP_FROM_OBJECT_mat4 = glGetUniformLocation(color.program, "CLIP_FROM_OBJECT");

	// ------ vertex buffer + vertex array object ------
	// (based on DrawLines.cpp)

	glGenVertexArrays(1, &vertex_buffer_for_color_program);
	glBindVertexArray(vertex_buffer_for_color_program);

	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

	glVertexAttribPointer(
		Position_vec4, //attribute
		2, //size
		GL_FLOAT, //type
		GL_FALSE, //normalized
		sizeof(GP26IntroMode::Vertex), //stride
		(GLbyte *)0 + offsetof(GP26IntroMode::Vertex, Position) //offset
	);
	glEnableVertexAttribArray(Position_vec4);

	glVertexAttribPointer(
		Color_vec4, //attribute
		4, //size
		GL_UNSIGNED_BYTE, //type
		GL_TRUE, //normalized
		sizeof(GP26IntroMode::Vertex), //stride
		(GLbyte *)0 + offsetof(GP26IntroMode::Vertex, Color) //offset
	);
	glEnableVertexAttribArray(Color_vec4);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindVertexArray(0);

	GL_ERRORS();

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
	//based on GP22IntroMode's draw

	constexpr glm::u8vec4 Bg1 = glm::u8vec4(0xff, 0xff, 0x00, 0xff);
	constexpr glm::u8vec4 Bg2 = glm::u8vec4(0xff, 0x88, 0x00, 0xff);

	//requested visible bounds:
	glm::vec2 scene_min = glm::vec2(-5.0f, -5.0f);
	glm::vec2 scene_max = glm::vec2( 5.0f,  5.0f);

	//computed visible bounds:
	glm::vec2 screen_min, screen_max;

	//compute matrix for vertex shader:
	glm::mat4 clip_from_object;

	{ //make sure scene_min - scene_max fits in drawable_size:
		float aspect = drawable_size.x / float(drawable_size.y);
		float scale = glm::min(
			2.0f * aspect / (scene_max.x - scene_min.x),
			2.0f / (scene_max.y - scene_min.y)
		);
		clip_from_object = glm::mat4(
			scale / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, scale, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			scale / aspect * -0.5f * (scene_max.x + scene_min.x), scale * -0.5f * (scene_max.y + scene_min.y), 0.0f, 1.0f
		);

		screen_min = 0.5f * (scene_max + scene_min) - glm::vec2(aspect, 1.0f) / scale;
		screen_max = 0.5f * (scene_max + scene_min) + glm::vec2(aspect, 1.0f) / scale;
	}
	
	//compute triangles to draw:
	std::vector< Vertex > attribs;
	
	size_t bg_start = attribs.size();
	//--- striped background ---
	{
		float angle = 52.0f / 180.0f * float(M_PI);
		glm::vec2 dir = glm::vec2(std::cos(angle), std::sin(angle));
		glm::vec2 perp = glm::vec2(-dir.y, dir.x);

		float ofs = time * -0.7f;

		float w = 0.7f;

		auto [perp_min, perp_max] = std::minmax({
			glm::dot(perp, glm::vec2(screen_min.x, screen_min.y)),
			glm::dot(perp, glm::vec2(screen_max.x, screen_min.y)),
			glm::dot(perp, glm::vec2(screen_min.x, screen_max.y)),
			glm::dot(perp, glm::vec2(screen_max.x, screen_max.y))
		});

		auto [dir_min, dir_max] = std::minmax({
			glm::dot(dir, glm::vec2(screen_min.x, screen_min.y)),
			glm::dot(dir, glm::vec2(screen_max.x, screen_min.y)),
			glm::dot(dir, glm::vec2(screen_min.x, screen_max.y)),
			glm::dot(dir, glm::vec2(screen_max.x, screen_max.y))
		});

		for (int32_t s = int32_t(std::floor(perp_min / w - ofs)); s <= int32_t(std::floor(perp_max / w - ofs)); ++s) {
			if (!attribs.empty()) attribs.emplace_back(attribs.back());
			attribs.emplace_back(perp * ((ofs + s) * w) + dir * dir_min, Bg1);
			if (attribs.size() != 1) attribs.emplace_back(attribs.back());
			attribs.emplace_back(perp * ((ofs + s + 0.5f) * w) + dir * dir_min, Bg1);
			attribs.emplace_back(perp * ((ofs + s) * w) + dir * dir_max, Bg1);
			attribs.emplace_back(perp * ((ofs + s + 0.5f) * w) + dir * dir_max, Bg1);
		}
	}

	stroke(&attribs,
		std::vector< glm::vec2 >{ glm::vec2(-5.0f, -5.0f), glm::vec2( 5.0f, 5.0f) },
		std::vector< float >{ 0.0f, 1.0f }, 0.5f,
		glm::u8vec4(0xff, 0xff, 0xff, 0xff), std::vector< std::pair< float, glm::u8vec4 > >{}, glm::u8vec4(0x00, 0x00, 0x00, 0xff)
	);

	size_t bg_end = attribs.size();

	//----- actually draw ----
	// (Based on DrawLines.cpp)

	//upload attribs:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, attribs.size() * sizeof(attribs[0]), attribs.data(), GL_STREAM_DRAW); //upload attribs array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//Clear background:
	glClearColor(Bg2.r / 255.0f, Bg2.g / 255.0f, Bg2.b / 255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//No depth test, please, but yes let's use alpha blending:
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(color.program);
	glUniformMatrix4fv(color.CLIP_FROM_OBJECT_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_object));

	glBindVertexArray(vertex_buffer_for_color_program);
	glDrawArrays(GL_TRIANGLE_STRIP, GLint(bg_start), GLsizei(bg_end - bg_start));
	glBindVertexArray(0);

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
