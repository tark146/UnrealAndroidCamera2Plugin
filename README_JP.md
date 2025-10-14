# Android Camera2 Plugin for Unreal Engine

[English](README.md) | 日本語

## 開発の動機
Unreal EngineにはCamera2 APIの公式サポートがなく、AndroidやMeta QuestプラットフォームでデバイスカメラにアクセスしたいXR開発者にとって大きな障壁となっています。このプラグインは、多くのUnreal開発者が不慣れな複雑なJava/JNI統合を処理することで、そのギャップを埋めます。

目標は、簡単なカメラアクセスを提供することで**Unreal EngineでのXR開発を加速**し、開発者がプラットフォーム固有の実装に苦労することなく、革新的なXR体験の創造に集中できるようにすることです。

## 概要
このプラグインは、AndroidおよびMeta Questデバイス向けに特別に設計されたUnreal EngineプロジェクトにCamera2 APIアクセスを提供します。リアルタイムカメラフィードのキャプチャとUnreal Engine内でのテクスチャとしての表示を可能にします。

## 機能
- Android Camera2 API統合
- UEテクスチャへのリアルタイムカメラプレビュー
- Meta Quest 3パススルーカメラのサポート
- フルカラーYUVからRGBAへの変換（色調整中）
- シンプルなBlueprintインターフェース

## サポートプラットフォーム
- Android（Meta Quest 2/3/Proを含む）
- Windows（エディタのみ - nullテクスチャを返します）

## 要件
- Unreal Engine 5.0以降
- Android SDK Level 21+（Android 5.0 Lollipop）
- Android Manifestでのカメラ権限

## 動作確認環境
- **Unreal Engine**: 5.3
- **デバイス**: Meta Quest 3
- **カメラID**: 50（Quest 3パススルーカメラ）
- **OS**: Questシステムソフトウェア（Androidベース）

## インストール

1. `AndroidCamera2Plugin`フォルダをプロジェクトの`Plugins`ディレクトリにコピー
2. プロジェクトファイルを再生成
3. プロジェクト設定または.uprojectファイルでプラグインを有効化

## 使い方

### Blueprintセットアップ

1. **カメラプレビューを開始:**
   ```
   SimpleCamera2Test::StartCameraPreview() -> bool
   ```
   カメラが正常に開始された場合trueを返す

2. **カメラテクスチャを取得:**
   ```
   SimpleCamera2Test::GetCameraTexture() -> Texture2D
   ```
   カメラフィードテクスチャを返す（開始されていない場合はnull）

3. **カメラプレビューを停止:**
   ```
   SimpleCamera2Test::StopCameraPreview()
   ```
   カメラを停止しリソースを解放

### サンプルBlueprintでクイックスタート

1. **提供されたサンプルを使用する場合:**
   - `BP_CameraImage`アクター（プラグインコンテンツ内）をレベルに配置
   - このアクターには完全なBlueprint実装を含むWidgetが含まれています
   - 実装の詳細については、Widget Blueprintを参考にしてください

2. **手動でBlueprint実装する場合:**
   - 新しいActor Blueprintを作成
   - PlaneまたはUI Imageコンポーネントを追加
   - BeginPlayで:
     - `StartCameraPreview`を呼び出す
     - `GetCameraTexture`でカメラテクスチャを取得
     - Dynamic Material Instanceを作成
     - テクスチャパラメータをカメラテクスチャに設定
     - マテリアルをPlane/Imageに適用

### C++使用方法

```cpp
#include "SimpleCamera2Test.h"

// カメラを開始
bool bSuccess = USimpleCamera2Test::StartCameraPreview();

// テクスチャを取得
UTexture2D* CameraTexture = USimpleCamera2Test::GetCameraTexture();

// カメラを停止
USimpleCamera2Test::StopCameraPreview();
```

## 権限

### 重要：初回起動時の権限ダイアログについて

**初回アプリ起動時**に、Androidでカメラアクセスの権限要求ダイアログが表示されます。すべてのカメラ権限を許可し、**アプリを再起動**することでカメラ機能が有効になります。

本来はUEの`ExtraPermissions`で自動付与されるはずですが、現在うまく動作していないため、申し訳ございません。現在の仕様です。

プラグインは以下の権限を自動的にAndroidマニフェストに追加します：

- `android.permission.CAMERA`
- `horizonos.permission.HEADSET_CAMERA`（Meta Quest）
- `horizonos.permission.AVATAR_CAMERA`（Meta Quest）

## 技術詳細

### アーキテクチャ
- **C++レイヤー**: Blueprint/C++アクセス用のUObjectベースインターフェース
- **JNIブリッジ**: ネイティブC++からJavaへの通信
- **Javaレイヤー**: ImageReaderを使用したCamera2 API実装
- **フレーム処理**: YUV_420_888からRGBAへの変換

### カメラ選択優先順位
1. Meta Quest特殊カメラ（ID 50、51） - パススルー用
2. フロントカメラ
3. バックカメラ
4. 利用可能な任意のカメラ（フォールバック）

### パフォーマンス
- デフォルト解像度: 320x240（コードで設定可能）
- フレームフォーマット: YUV_420_888 -> BGRA8
- 処理: カメラ操作用バックグラウンドスレッド
- テクスチャ更新: ゲームスレッド同期

### 解像度の変更方法

デフォルト解像度は800x600ですが、他のサポートされた解像度に変更できます。**重要**: メモリ破損を避けるため、3箇所すべてを一貫して更新する必要があります。

**サポートされている解像度（Quest 3）:**
- 320x240
- 640x480
- 800x600（デフォルト）
- 1280x960

**必要な変更:**

1. **Java側** - `Source/AndroidCamera2Plugin/Android/src/com/epicgames/ue4/Camera2Helper.java`を編集:
```java
private int frameWidth = 800;   // ここを変更
private int frameHeight = 600;  // ここを変更
```

2. **C++側（テクスチャ作成）** - `Source/AndroidCamera2Plugin/Private/SimpleCamera2Test.cpp`を編集:
```cpp
CameraTexture = UTexture2D::CreateTransient(800, 600, PF_B8G8R8A8);
```

3. **C++側（メモリ初期化）** - 同じファイルの数行下:
```cpp
FMemory::Memset(TextureData, 64, 800 * 600 * 4); // 幅 * 高さ * 4(RGBA)
```

**変更後:**
1. ビルドフォルダをクリーン: プラグインとプロジェクト両方の`Intermediate`と`Binaries`フォルダを削除
2. プロジェクトファイルを再生成
3. プロジェクトをリビルド

⚠️ **警告**: この3つの値が一致しないと、メモリアクセス違反やテクスチャ破損が発生します。

### 現在の制限事項（v1.0）
- **色精度の問題** - フルカラーYUVからRGB変換は実装済みですが、暖色系/オレンジがかった色合いになる場合があります
- **色空間キャリブレーション必要** - Quest 3カメラは特定の色空間を使用しており微調整が必要
- これらは初期リリースの一時的な制限です

## 既知の問題
- カメラの自動露出が調整されるまで、初期フレームが暗く表示される場合があります
- Quest 3パススルーカメラには特別な権限が必要です
- カメラプレビューはエディタでは動作しません（Androidのみ）

## トラブルシューティング

### カメラが開始しない
- Androidログキャットで権限エラーを確認
- カメラ権限が付与されていることを確認
- 他のアプリがカメラを使用していないことを確認

### 黒/暗いテクスチャ
- カメラの自動露出に時間が必要な場合があります
- YUVデータが受信されているか確認（ログ参照）
- テクスチャフォーマットの互換性を確認

### Quest固有の問題
- Quest固有の権限が付与されていることを確認
- パススルーカメラにはカメラID 50または51を試す
- システム設定でパススルーが有効になっているか確認

## ライセンス
MITライセンス - 詳細はLICENSEファイルを参照

## 開発ノート
このプラグインはAIの支援を受けながら、膨大な試行錯誤を通じて開発されました。AIがコード生成と問題解決を支援しましたが、実際の実装にはCamera2 APIをUnreal Engineで適切に動作させるために、無数の反復、デバッグセッション、実機テストが必要でした。「カメラが見つからない」から「リアルタイムテクスチャストリーミング」への道のりは課題だらけでしたが、まさにそれがこのプラグインが存在する必要がある理由です - 他の人が同じ苦労をする必要がないように。

## 作者
TARK (Olachat)

## サポート
問題や質問については、GitHubリポジトリでIssueを作成してください。

## 変更履歴

### バージョン 1.0
- 初回リリース
- 基本的なCamera2 API統合
- Meta Questサポート
- グレースケールプレビュー（YUV Yチャンネルのみ）
- 320x240固定解像度
- 注：カラーサポートと設定可能な解像度は将来のバージョンで計画