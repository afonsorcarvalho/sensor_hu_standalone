# Script para configurar o repositório remoto no GitHub
# Execute este script após criar o repositório no GitHub

Write-Host "=" -NoNewline
Write-Host ("=" * 79)
Write-Host "CONFIGURAÇÃO DO REPOSITÓRIO REMOTO GITHUB"
Write-Host ("=" * 80)
Write-Host ""

$repoUrl = Read-Host "Digite a URL do seu repositório no GitHub (ex: https://github.com/usuario/repo.git)"

if ([string]::IsNullOrWhiteSpace($repoUrl)) {
    Write-Host "Erro: URL não fornecida!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Configurando o repositório remoto..." -ForegroundColor Yellow

# Adiciona o remote
git remote add origin $repoUrl

if ($LASTEXITCODE -eq 0) {
    Write-Host "✓ Remote 'origin' adicionado com sucesso!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Fazendo push para o GitHub..." -ForegroundColor Yellow
    
    # Faz o push
    git push -u origin master
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "✓ Push realizado com sucesso!" -ForegroundColor Green
        Write-Host "Seu repositório está agora conectado ao GitHub!" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "✗ Erro ao fazer push. Verifique se:" -ForegroundColor Red
        Write-Host "  - A URL está correta" -ForegroundColor Red
        Write-Host "  - Você tem permissão para fazer push" -ForegroundColor Red
        Write-Host "  - O repositório existe no GitHub" -ForegroundColor Red
    }
} else {
    Write-Host "✗ Erro ao adicionar remote. Pode ser que já exista." -ForegroundColor Red
    Write-Host "Verifique com: git remote -v" -ForegroundColor Yellow
}
