# Como conectar este repositório ao GitHub

## Passo 1: Criar repositório no GitHub

1. Acesse https://github.com e faça login
2. Clique no botão "+" no canto superior direito
3. Selecione "New repository"
4. Preencha:
   - **Repository name**: `sensor-umidade-standalone` (ou o nome que preferir)
   - **Description**: "Sensor de Umidade ESP32 - Sistema Standalone com análise psicrométrica"
   - Escolha se será **Public** ou **Private**
   - **NÃO** marque "Initialize this repository with a README" (já temos arquivos)
   - **NÃO** adicione .gitignore (já temos)
   - **NÃO** escolha uma license ainda (ou escolha uma se quiser)
5. Clique em "Create repository"

## Passo 2: Copiar a URL do repositório

Após criar, o GitHub mostrará uma página com comandos. Copie a URL:
- **HTTPS**: `https://github.com/SEU_USUARIO/NOME_DO_REPO.git`
- **SSH**: `git@github.com:SEU_USUARIO/NOME_DO_REPO.git`

## Passo 3: Executar os comandos abaixo

Substitua `URL_DO_SEU_REPOSITORIO` pela URL que você copiou:

```bash
git remote add origin URL_DO_SEU_REPOSITORIO
git branch -M main
git push -u origin main
```

Ou se preferir usar a branch master:

```bash
git remote add origin URL_DO_SEU_REPOSITORIO
git push -u origin master
```

## Nota

Se você já tem commits na branch `master` localmente, pode manter o nome `master` ao invés de renomear para `main`.
