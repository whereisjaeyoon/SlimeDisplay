PS > Get-PnpDevice -Class Display |
>>     Where-Object { $_.FriendlyName -eq 'Virtual Display Driver' } |
>>     Enable-PnpDevice -Confirm:$false
PS > Get-PnpDevice -Class Display |
>>     Where-Object { $_.FriendlyName -eq 'Virtual Display Driver' } |
>>     Disable-PnpDevice -Confirm:$false


VDD usage