#define _CRT_SECURE_NO_WARNINGS

#define TL_IMPL
#include <tl/main.h>
#include <tl/file.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

using namespace tl;

void print_usage() {
	print(R"(Usage:
Parameter  Value                       Description
-i         image path                  Image to store data in
-d         data file path              Data to store in the image
-b         1 or 2 or 4 (default is 2)  How much bits to take from each channel for storing data
-e         (default is off)            Decode data from image into data file
)");
}

struct ImageEncoder {
	Span<u8> channels;
	u32 current_channel_index = 0;
	u32 mask = 0;
	u32 bits_per_channel = 0;
	u32 available_bits = 0;
	u32 bits_written = 0;
};

ImageEncoder create_image_encoder(Span<u8> channels, u32 bits_per_channel) {
	ImageEncoder result = {};
	result.channels = channels;
	result.bits_per_channel = bits_per_channel;
	result.available_bits = channels.count * bits_per_channel;

	switch (bits_per_channel) {
		case 1: result.mask = 1; break;
		case 2: result.mask = 3; break;
		case 4: result.mask = 15; break;
	}
	return result;
}

bool write(ImageEncoder &encoder, Span<u8> data) {
	encoder.bits_written += data.count * 8;
	if (encoder.bits_written > encoder.available_bits) {
		return false;
	}

	u32 remaining_bits = data.count * 8;

	u8 remaining_value = 0;
	u8 remaining_value_bits = 0;

	while (remaining_bits) {
		if (remaining_value_bits == 0) {
			remaining_value_bits = 8;
			remaining_value = data.front();
			data.data += 1;
			data.count -= 1;
		}
		auto result = remaining_value & encoder.mask;
		remaining_value >>= encoder.bits_per_channel;
		remaining_value_bits -= encoder.bits_per_channel;

		print("%\n", FormatInt{.value=result, .radix=2,.leading_zero_count=4});

		encoder.channels[encoder.current_channel_index] = (encoder.channels[encoder.current_channel_index] & ~encoder.mask) | result;

		encoder.current_channel_index += 1;
		remaining_bits -= encoder.bits_per_channel;
	}

	return true;
}

struct ImageDecoder {
	Span<u8> channels;
	u32 current_channel_index = 0;
	u32 mask = 0;
	u32 bits_per_channel = 0;
};

ImageDecoder create_image_decoder(Span<u8> channels, u32 bits_per_channel) {
	ImageDecoder result;
	result.channels = channels;
	result.bits_per_channel = bits_per_channel;
	switch (bits_per_channel) {
		case 1: result.mask = 1; break;
		case 2: result.mask = 3; break;
		case 4: result.mask = 15; break;
	}
	return result;
}

bool read(ImageDecoder &decoder, Span<u8> destination) {
	u8 *dest_byte = destination.data;
	*dest_byte = 0;
	u8 bits_read_from_byte = 0;

	while (1) {

		if (decoder.current_channel_index == decoder.channels.count) {
			return false;
		}

		auto val = (decoder.channels[decoder.current_channel_index] & decoder.mask);
		*dest_byte |= val << bits_read_from_byte;
		decoder.current_channel_index += 1;


		bits_read_from_byte += decoder.bits_per_channel;
		if (bits_read_from_byte == 8) {
			dest_byte += 1;
			if (dest_byte == destination.end()) {
				break;
			}
			*dest_byte = 0;
			bits_read_from_byte = 0;
		}
	}


	return true;
}

s32 tl_main(Span<Span<utf8>> arguments) {
	if (arguments.count < 2) {
		print_usage();
	}

	Span<utf8> image_path;
	Span<utf8> data_path;
	u32 bits_per_channel = 2;
	bool decode = false;

	for (u32 i = 1; i < arguments.count; ++i) {
		if (arguments[i] == u8"-i"s) {
			++i;
			if (i >= arguments.count) {
				print("No image path provided after -i\n");
				return 1;
			}
			image_path = arguments[i];
		} else if (arguments[i] == u8"-d"s) {
			++i;
			if (i >= arguments.count) {
				print("No data path provided after -d\n");
				return 1;
			}
			data_path = arguments[i];
		} else if (arguments[i] == u8"-b"s) {
			++i;
			if (i >= arguments.count) {
				print("No bit count provided after -b\n");
				return 1;
			}
			auto parsed_bits_per_channel = parse_u64(arguments[i]);
			if (!parsed_bits_per_channel) {
				print("Failed to parse bit count\n");
				return 1;
			}
			bits_per_channel = parsed_bits_per_channel.value_unchecked();

			if (bits_per_channel != 1 && bits_per_channel != 2 && bits_per_channel != 4) {
				print("Invalid bit count. Available values are 1, 2 or 4\n");
				return 1;
			}
		} else if (arguments[i] == u8"-e"s) {
			decode = true;
		} else {
			print("Unknown argument '%'\n", arguments[i]);
			return 1;
		}
	}

	if (!image_path.data) {
		print("No image path provided\n");
		return 1;
	}
	if (!data_path.data) {
		print("No data path provided\n");
		return 1;
	}

	auto image_data = read_entire_file(image_path);
	if (!image_data.data) {
		print("Failed to read from '%'\n", image_path);
		return 1;
	}
	defer { free(image_data); };

	int width, height;
	auto pixels = stbi_load_from_memory(image_data.data, image_data.count, &width, &height, 0, 3);
	if (!pixels) {
		print("Failed load image '%'\n", image_path);
		return 1;
	}
	defer { stbi_image_free(pixels); };

	u32 mask = 0;
	switch (bits_per_channel) {
		case 1: mask = 1; break;
		case 2: mask = 3; break;
		case 4: mask = 15; break;
	}

	if (decode) {
		auto decoder = create_image_decoder(Span(pixels, width*height*3), bits_per_channel);

		u32 data_size = 0;

		if (!read(decoder, value_as_bytes(data_size))) {
			print("Failed to decode an image. (data_size)\n");
			return 1;
		}

		auto data = create_buffer(data_size);
		defer { free(data); };

		if (!read(decoder, data)) {
			print("Failed to decode an image. (data)\n");
			return 1;
		}

		write_entire_file(data_path, data);
	} else {
		auto data = read_entire_file(data_path);
		if (!data.data) {
			print("Failed to read from '%'\n", data_path);
			return 1;
		}
		defer { free(data); };


		auto encoder = create_image_encoder(Span(pixels, width*height*3), bits_per_channel);

		if (!write(encoder, value_as_bytes((u32)data.count)) ||
			!write(encoder, data))
		{
			print("Unable to store data in the image. There is not enough space\n");
			return 1;
		}


		if (stbi_write_bmp("output.bmp", width, height, 3, pixels) == 0) {
			print("Failed to write output.bmp\n");
			return 1;
		}
	}

	return 0;
}
