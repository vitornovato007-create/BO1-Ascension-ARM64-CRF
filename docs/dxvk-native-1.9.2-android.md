# DXVK Native 1.9.2 — integração experimental para Android ARM64

Status: infraestrutura de build e empacotamento adicionada; **ainda não compilada nem testada em dispositivo**.

## Fonte fixada

- Repositório: https://github.com/q4a/dxvk-native
- Commit: `dedfa1877afb9b9d30aaaa5935d56238ae12bb8b`
- Versão declarada pelo projeto: `v1.9.2`
- WSI: SDL2
- ABI alvo inicial: Android `arm64-v8a`

A árvore é DXVK Native, com os headers D3D9 Native e a integração SDL2 que o bridge do Ascension utiliza. Não é uma DLL Wine/Proton renomeada.

## O que foi adicionado nesta branch

- `tools/native/build-dxvk-native-1.9.2.sh`: clona o commit fixo e constrói a biblioteca nativa em diretório separado:
  `/bo1/dxvk-native-1.9.2/build-android-arm64-v8a/src/d3d9/libdxvk_d3d9.so`.
- `tools/native/build-apk.sh`: aceita `DXVK_DIR` para empacotar a biblioteca e a licença da árvore selecionada. Sem essa variável, prefere automaticamente a árvore 1.9.2 quando a biblioteca ARM64 já foi construída; caso contrário, mantém a árvore antiga `/bo1/dxvk` (DXVK 2.6.1).
- `src/native/vk/bo1vk_dxvk.cpp`: a mensagem de diagnóstico não presume mais que todo DXVK exija Vulkan 1.3.

## Sequência de build no ambiente do projeto

1. Garanta que o SDL2 Android já foi construído. Se ainda não existir em `/bo1/android/sdl/lib/libSDL2.so`, execute primeiro o build Android normal para gerar o SDL2.
2. Construa a variante Native 1.9.2:

   ```bash
   bash tools/native/build-dxvk-native-1.9.2.sh arm64-v8a
   ```

3. Reconfigure o engine para compilar o bridge com os headers da mesma versão. O script agora prefere automaticamente os headers 1.9.2 quando a árvore existe. Como ele reutiliza o diretório CMake existente, remova **somente o diretório de build Android do engine** antes da reconfiguração:

   ```bash
   rm -rf /bo1/p32tree/build/android
   bash tools/native/build-android.sh
   ```

4. Empacote a biblioteca 1.9.2 no nome padrão esperado pelo app (`libdxvk_d3d9.so`):

   ```bash
   bash tools/native/build-apk.sh
   ```

A etapa 3 é importante: compilar o engine contra os headers 2.6.1 e carregar a implementação 1.9.2 não é o teste controlado desejado. A versão selecionada para headers e biblioteca precisa corresponder.

## O que ainda não foi portado

Os patches específicos de 2.6.1 do script atual não foram copiados para 1.9.2. Isso é intencional: os patches de apresentação `VK_SUBOPTIMAL_KHR`, compilação assíncrona de pipelines e seleção de multisample resolve dependem de trechos internos que mudaram entre versões. A variante 1.9.2 começa sem esses patches, para evitar aplicar alterações incompatíveis ou mascarar erros de integração.

Consequências possíveis no teste:

- mais stutter na primeira utilização de shaders/pipelines;
- comportamento diferente em telas/dispositivos Android com transformações de superfície;
- diferença de desempenho, estabilidade ou compatibilidade no Mali-G76 — ainda desconhecida.

O script prepara somente o alvo Android ARM64 para este primeiro teste. Não adiciona ainda um seletor visual no launcher, nem substitui ou apaga a árvore 2.6.1.

## Critérios para considerar a integração validada

- build da biblioteca concluído sem erros;
- `readelf -d` confirma dependências esperadas (`libSDL2.so`, `libvulkan.so`) e nenhum SONAME inesperado;
- APK contém `lib/arm64-v8a/libdxvk_d3d9.so`;
- log confirma `Direct3DCreate9` resolvido e criação de dispositivo bem-sucedida;
- teste real no Galaxy S10 registra FPS, frame time, estabilidade e artefatos na mesma cena/resolução usada com 2.6.1.

Nenhum workflow foi executado nesta alteração e nenhum APK foi produzido ou testado.

## Compilação pelo GitHub Actions

Foi adicionado o workflow manual `.github/workflows/android.yml` tanto à branch de trabalho quanto à branch padrão `main` (necessário para o GitHub habilitar `workflow_dispatch`). Na aba Actions, selecione o workflow e, no campo de branch, escolha `crf/dxvk-legacy-audit`:

1. Abra a aba **Actions** do repositório.
2. Selecione **Build Android APK (DXVK Native 1.9.2)**.
3. Clique em **Run workflow** e confirme a branch `crf/dxvk-legacy-audit`.
4. Aguarde o job terminar. O APK aparecerá em **Artifacts** com o nome `bo1-android-arm64-dxvk-native-1.9.2`; os logs ficam em um segundo artifact.

O workflow executa a sequência necessária para resolver a dependência circular entre SDL2 e DXVK Native: primeiro gera SDL2 com uma configuração temporária do engine sem DXVK, depois compila DXVK Native 1.9.2, regenera/reconfigura o engine com os headers 1.9.2 e finalmente empacota o APK. O APK só é enviado se existir e não estiver vazio.

O workflow foi adicionado, mas **não foi executado nesta alteração**. A primeira execução é também o primeiro teste real do script Meson/cross-compilation em runner limpo; se houver incompatibilidade de opção, dependência ou limite de espaço/tempo do runner, os logs serão necessários para corrigir.
