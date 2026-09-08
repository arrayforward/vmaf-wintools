package com.vmaftool.player

import android.app.Activity
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.annotation.OptIn
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.AspectRatioFrameLayout
import androidx.media3.ui.PlayerView
import java.io.File

/**
 * 云游戏画质评测 - 云侧测试片源播放器。
 *
 * 行为：
 * - 启动即全屏循环播放 assets/test_video.mp4（黑底、无控件、音量 0）。
 * - 可用 intent extra "video_path" 指定外部文件路径覆盖 assets（调试用）。
 * - 找不到片源时不崩溃，显示纯黑画面并输出日志，便于 adb 截图流程继续。
 */
@OptIn(UnstableApi::class)
class MainActivity : Activity() {

    private var player: ExoPlayer? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setupImmersiveFullscreen()

        val playerView = PlayerView(this).apply {
            useController = false
            resizeMode = AspectRatioFrameLayout.RESIZE_MODE_FIT
            setBackgroundColor(android.graphics.Color.BLACK)
            keepScreenOn = true
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        }
        setContentView(playerView)

        player = ExoPlayer.Builder(this).build().also { exo ->
            playerView.player = exo
            exo.repeatMode = Player.REPEAT_MODE_ONE
            exo.volume = 0f

            val uri = resolveVideoUri()
            if (uri != null) {
                exo.setMediaItem(MediaItem.fromUri(uri))
                exo.prepare()
                exo.playWhenReady = true
                Log.i(TAG, "Playing: $uri")
            } else {
                // 无片源：保持纯黑画面，不崩溃
                Log.w(TAG, "No video source found, showing black screen only.")
            }
        }
    }

    /**
     * 片源解析优先级：
     * 1. intent extra "video_path" 指向的外部文件（存在才生效）
     * 2. assets/test_video.mp4（存在才生效）
     * 否则返回 null。
     */
    private fun resolveVideoUri(): Uri? {
        val externalPath = intent.getStringExtra(EXTRA_VIDEO_PATH)
        if (!externalPath.isNullOrBlank()) {
            val f = File(externalPath)
            if (f.isFile) {
                return Uri.fromFile(f)
            }
            Log.w(TAG, "video_path not found or not a file: $externalPath, fallback to assets")
        }
        return if (assetExists(ASSET_VIDEO_NAME)) {
            Uri.parse("asset:///$ASSET_VIDEO_NAME")
        } else {
            Log.w(TAG, "assets/$ASSET_VIDEO_NAME missing. " +
                    "Put the burned-QR test video there and rename it to $ASSET_VIDEO_NAME.")
            null
        }
    }

    private fun assetExists(name: String): Boolean = try {
        assets.open(name).close()
        true
    } catch (e: Exception) {
        false
    }

    private fun setupImmersiveFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // 云侧桌面/分屏等场景下焦点变化后重新进入沉浸式
        if (hasFocus) setupImmersiveFullscreen()
    }

    override fun onStop() {
        super.onStop()
        player?.playWhenReady = false
    }

    override fun onStart() {
        super.onStart()
        // 回到前台自动恢复播放（仅当已加载片源）
        if (player?.mediaItemCount ?: 0 > 0) {
            player?.playWhenReady = true
        }
    }

    override fun onDestroy() {
        player?.release()
        player = null
        super.onDestroy()
    }

    companion object {
        private const val TAG = "VmafPlayer"
        const val EXTRA_VIDEO_PATH = "video_path"
        const val ASSET_VIDEO_NAME = "test_video.mp4"
    }
}
