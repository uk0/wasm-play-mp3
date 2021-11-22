#include <mad.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define X86_64

#ifndef X86_64
#include <emscripten.h>
#endif

// 上文所述的上下文变量的结构体，用来保存解码的 PCM 数据和其他信息
struct buffer {
  uint8_t* input;
  size_t input_length;
  mad_fixed_t** output;
  size_t output_length;
  size_t output_channels;
  size_t output_samples;
};

// native 环境下 decode_mp3_to_pcm 函数的返回值
// 主函数通过读取这些信息将 PCM 写入存储
// wasm 环境下没用到这个结构体
struct output_t {
  mad_fixed_t** p_output;
  size_t channels;
  size_t samples;
};

// 输入回调
static enum mad_flow input_callback (void *data, struct mad_stream *stream) {
  struct buffer *buffer = (struct buffer *)data;

  // 已经没有数据了，直接不管了，返回结束
  if (buffer->input_length == 0) {
    return MAD_FLOW_STOP;
  }

  // 直接把整个 MP3 丢进去……反正又不是不能用（
  mad_stream_buffer(stream, buffer->input, buffer->input_length);
  buffer->input_length = 0;
  return MAD_FLOW_CONTINUE;
}

// 输出回调
static enum mad_flow output_callback (void *data, struct mad_header const *header, struct mad_pcm *pcm) {
  struct buffer *buffer = (struct buffer *)data;
  uint16_t samples;
  int i;

  samples = pcm->length;

  if (!buffer->output) {
    // 首次输出，分配好内存啥的
    puts("output func fired");
    buffer->output = (mad_fixed_t **)malloc(pcm->channels * sizeof(mad_fixed_t *));
    buffer->output_channels = pcm->channels;

    for (i = 0; i != pcm->channels; ++i) {
      // 到后面再 realloc，这个不算未定义行为吧
      buffer->output[i] = (mad_fixed_t *) malloc(0);
    }
  }

  #ifdef X86_64
  // 对于 native 环境这里用 realloc 就行了，虽然很粗暴，不过性能也不会差到哪儿去（丢人！）
  for (i = 0; i != pcm->channels; ++i) {
    buffer->output[i] = (mad_fixed_t *) realloc(buffer->output[i], (buffer->output_samples + pcm->length) * sizeof(mad_fixed_t));
  }

  // 逐个采样写入到输出数组
  for (i = 0; i != pcm->length; ++i) {
    int channel = 0;
    for (channel = 0; channel != pcm->channels; ++channel) {
      buffer->output[channel][buffer->output_samples + i] = pcm->samples[channel][i];
    }
  }

  #else
  // 至于 WASM ……是真的慢，不如直接把数据通过 EM_ASM_ 直接写入给 JavaScript 上下文，速度还是很快的……
  EM_ASM_({
    const begin = new Date().getTime();
    const pcm_l_addr = $0;
    const pcm_r_addr = $1;
    const frames = $2;
    const offset = $3;

    if (!window.pcm) {
      // 这里如果直接用字面值构造变量，emcc 会和我说 left 和 right 这两个变量为定义
      // 就很神秘，不过显然走 JSON.parse 就没这个问题啦
      window.pcm = JSON.parse("{ \"left\": [], \"right\": [] }");
      window.begin = new Date().getTime();
      window.jscost = 0;
    }
    let i = 0;
    for (i = 0; i < frames; ++i) {
      // 这里的 HEAP32 可以理解为一个 int32_t 的数组，里面就是 WASM 的堆内存
      // 我们从 EM_ASM_ 宏的参数中传入了两个 PCM 采样数组的地址，计算偏移就能拿到数据了
      // 我们直接把数据写入 JS 数组，反正众所周知 JS 的数组是稀疏数组，也不需要人为的扩展大小
      window.pcm.left[offset + i] = Module.HEAP32[pcm_l_addr / 4 + i];
      window.pcm.right[offset + i] = Module.HEAP32[pcm_r_addr / 4 + i];
    }

    window.jscost += (new Date().getTime()) - begin;
  }, pcm->samples[0], pcm->samples[1], pcm->length, buffer->output_samples);
  #endif

  buffer->output_samples += pcm->length;

  // 当然是继续啦，就算没有数据了这个函数也看不出来，反正没数据了 libmad 也不会再去执行这个回调了
  return MAD_FLOW_CONTINUE;
}

// 异常回调
static enum mad_flow error_callback(void *data, struct mad_stream *stream, struct mad_frame *frame) {
  puts("error func fired");
  // doing nothing（
  return MAD_FLOW_CONTINUE;
}

// 解码函数，在这里构造 libmad 的相关数据并执行解码，目的是可以直接被 wasm 拿去用
#ifndef X86_64
EMSCRIPTEN_KEEPALIVE void
#else
struct output_t
#endif
decode_mp3_to_pcm(uint8_t* input, size_t input_size) {
  struct buffer buffer;
  struct mad_decoder decoder;
  struct output_t output;
  int r;

  memset(&buffer, 0, sizeof(buffer));
  buffer.input = input;
  buffer.input_length = input_size;

  mad_decoder_init(&decoder, &buffer, input_callback, NULL, NULL, output_callback, error_callback, NULL);
  mad_decoder_options(&decoder, 0);
  r = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
  mad_decoder_finish(&decoder);

  output.p_output = buffer.output;
  output.samples = buffer.output_samples;
  output.channels = buffer.output_channels;
  #ifdef X86_64
  return output;
  #else
  EM_ASM_({
    console.log('WASM part finished, duration: ' + ((new Date().getTime()) - window.begin - window.jscost) + 'ms');
    const channels = $0;
    const samples = $1;
    const pcm_l_addr = $2;
    const pcm_r_addr = $3;

    let ctx = new AudioContext(); // 创建音频上下文
    let frames = samples;
    let audioBuffer = ctx.createBuffer(channels, frames, 44100); // 创建音频缓冲区
    let left = audioBuffer.getChannelData(0); // 获得两个声道的 PCM 数据数组
    let right = audioBuffer.getChannelData(1);
    let i = 0;

    // 往两个数组里写入数据，值得注意的是这里每个采样的取值范围是 [-1. 1]，
    // 如果直接把 libmad 的输出 (mad_fixed_t aka signed long）写进去
    // 回放的效果大概就是原歌曲音量放大 2 ^ 30 并强行限幅后的方波音乐了吧（
    // 反正第一次我忘了这事儿，回放出来的东西把我吓死了（
    for (i = 0; i < frames; ++i) {
      left[i] = window.pcm.left[i] / (2 ** 30);
      right[i] = window.pcm.right[i] / (2 ** 30);
    }

    // 将音频缓冲变为音频源的数据源，然后输出到音频上下文
    let source = ctx.createBufferSource();
    source.buffer = audioBuffer;
    source.connect(ctx.destination);
    // 开始播放咯
    source.start();
  }, output.channels, output.samples, output.p_output[0], output.p_output[1]);
  #endif
}

#ifndef X86_64
EMSCRIPTEN_KEEPALIVE
#endif
// 当时拿来测试能不能成功执行一个函数的，懒得删了
void hello () {
  printf("hello world\n");
}

#ifndef X86_64
EMSCRIPTEN_KEEPALIVE
#endif
// 测试 typed array 和 wasm 数组的转换
// 懒得删了
void test(uint8_t *array, size_t length) {
  int i = 0;
  for (i = 0; i != length; ++i) {
    printf("%d ", array[i]);
  }
  printf("\n");
}

#ifndef X86_64
EMSCRIPTEN_KEEPALIVE
#endif
int main() {
  #ifdef X86_64
  // native 环境直接打开文件转换然后写入到外部的一个文件
  // 就行了，主要是测试解码的代码能不能跑通的
  FILE* fp = fopen("audio.mp3", "rb");
  size_t file_size = 0;
  uint8_t *input;
  int i;
  hello();
  if (!fp) {
    printf("Fuck you\n");
    return 1;
  }

  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);

  input = (uint8_t *) malloc(file_size);
  fseek(fp, 0, SEEK_SET);
  fread(input, file_size, 1, fp);
  fclose(fp); fp = NULL;

  struct output_t output = decode_mp3_to_pcm(input, file_size);

  fp = fopen("output.pcm", "wb+");
  for (i = 0; i != output.samples; ++i) {
    int channel = 0;
    for (channel = 0; channel != output.channels; ++channel) {
      fwrite(&output.p_output[channel][i], sizeof(output.p_output[channel][i]), 1, fp);
    }
  }
  fclose(fp);
  #else
  // WASM 环境不需要执行 main，而是通过 JS 直接调用某一个 export 出来的函数
  // 随便跑点东西搞点输出，意思一下就行了（
  hello();
  #endif
  return 0;
}